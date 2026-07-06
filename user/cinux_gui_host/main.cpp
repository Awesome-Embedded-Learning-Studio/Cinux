/**
 * @file user/cinux_gui_host/main.cpp
 * @brief CinuxOS userspace GUI host adapter (F-GUI-USERSPACE b3a + b3b step1)
 *
 * Drives the Cinux-GUI host-neutral core (21 freestanding C++ sources) as a
 * userspace process: opens /dev/fb0 (mmap display) + /dev/event0 (input),
 * builds a widget tree (WindowManager + Window + Label + Desktop), and pumps
 * GuiCore which drains input, renders into the staging buffer, and flushes
 * dirty rects to the mmap'd framebuffer. The Host ABI table (core/host.hpp)
 * is the only seam to the core.
 *
 * b3a: fb mmap + render + readback smoke (poll_event NULL).
 * b3b step1: poll_event reads /dev/event0 via poll(0) (non-blocking) and maps
 * kernel Events to EventHeader + Pointer/Keycode payloads; dispatch_event feeds
 * them to the WM/Desktop. Open with O_RDONLY; poll(0) returns 0 when idle so
 * pump never blocks.
 *
 * Smoke mode: argv[1] = pump count (0 = forever). After N pumps read back the
 * fb center pixel and exit 0 if non-zero. Mirrors host/linux_fbdev_main.cpp +
 * kernel/gui/host_cinux.cpp (ABI fill + widget tree + flush row-blit +
 * poll_event payload map).
 */

#include <fcntl.h>
#include <poll.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>

#include "event.hpp"          // EventHeader, EventCode, kEventMagic, kAbiVersion
#include "event_payload.hpp"  // PointerPayload, KeycodePayload, kPointerKind*, kKeymod*
#include "font.hpp"
#include "gui_core.hpp"
#include "host.hpp"
#include "region.hpp"
#include "swraster.hpp"
#include "theme.hpp"
#include "widget.hpp"
#include "widget/label.hpp"
#include "widget/window.hpp"
#include "widget/window_manager.hpp"

// Mirror of kernel/drivers/video/fb_dev.hpp kFbioGetScreenInfo + FbScreenInfo.
#define FBIOGET_SCREENINFO 0x4600
struct fb_screen_info {
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t bpp;
};

namespace {

using namespace cinux::gui;

constexpr uint32_t kWinW = 200;
constexpr uint32_t kWinH = 160;

// Mirror of kernel/gui/event.hpp cinux::gui::Event -- the /dev/event0 wire
// format (batch 2 InputEventDeviceOps::read returns this struct raw). CinuxOS
// and this host are both gcc/SysV x86_64, so the C++ POD layout matches.
enum kernel_event_type : uint8_t {
    kMove      = 0,
    kMouseDown = 1,
    kMouseUp   = 2,
    kKeyDown   = 3,
    kKeyUp     = 4,
};
struct kernel_mouse_event {
    int32_t x;
    int32_t y;
    int32_t dx;
    int32_t dy;
    uint8_t buttons;
    bool    left;
    bool    right;
    bool    middle;
};
struct kernel_key_event {
    char    ascii;
    uint8_t scancode;
    bool    pressed;
    bool    shift;
    bool    ctrl;
    bool    alt;
};
struct kernel_event {
    uint8_t type_;
    union {
        kernel_mouse_event mouse;
        kernel_key_event   key;
    };
};

struct HostState {
    uint8_t*      fb;  // mmap'd /dev/fb0 base
    uint32_t      fb_w;
    uint32_t      fb_h;
    uint32_t      fb_pitch;
    int           ev_fd;  // /dev/event0 (-1 if absent -> poll_event no-op)
    PsfFont       font;
    Theme         theme;
    WindowManager wm;
    Window        win;
    Label         content;
    Desktop       desktop;
};

// L1 Display: flush one dirty rect from staging to the mmap'd framebuffer.
// Mirrors host_cinux.cpp::cinux_flush (clamp + row-blit) but non-volatile target.
void host_flush(void* ctx, int x, int y, int w, int h, const void* pixels, uint32_t stride,
                PixelFormat fmt) {
    auto* st = static_cast<HostState*>(ctx);
    if (st->fb == nullptr || pixels == nullptr || w <= 0 || h <= 0) {
        return;
    }
    if (fmt != PixelFormat::kXrgb8888) {
        return;
    }
    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (w <= 0 || h <= 0 || x >= static_cast<int>(st->fb_w) || y >= static_cast<int>(st->fb_h)) {
        return;
    }
    if (x + w > static_cast<int>(st->fb_w)) {
        w = static_cast<int>(st->fb_w) - x;
    }
    if (y + h > static_cast<int>(st->fb_h)) {
        h = static_cast<int>(st->fb_h) - y;
    }
    const uint32_t  src_stride_px = stride / 4u;
    const uint32_t  dst_stride_px = st->fb_pitch / 4u;
    const uint32_t* src           = static_cast<const uint32_t*>(pixels);
    uint32_t*       dst           = reinterpret_cast<uint32_t*>(st->fb);
    for (int row = 0; row < h; ++row) {
        const uint32_t  py = static_cast<uint32_t>(y + row);
        const uint32_t* srow =
            src + static_cast<size_t>(py) * src_stride_px + static_cast<uint32_t>(x);
        uint32_t* drow = dst + static_cast<size_t>(py) * dst_stride_px + static_cast<uint32_t>(x);
        for (int p = 0; p < w; ++p) {
            drow[p] = srow[p];
        }
    }
}

// L4 render: paint the widget tree into the core-owned staging + report dirty.
void host_render_frame(void* ctx, Frame* frame) {
    auto* st = static_cast<HostState*>(ctx);
    if (frame == nullptr) {
        return;
    }
    Surface s{frame->pixels, frame->width, frame->height, frame->stride, frame->format};
    Region  dirty;
    st->desktop.render(s, st->font, &dirty);
    const uint32_t n = dirty.count();
    for (uint32_t i = 0; i < n && i < frame->max_rects; ++i) {
        frame->rects[i] = dirty.rects()[i];
    }
    frame->count = n;
}

// L2 Input: drain one event from /dev/event0. poll(0) makes it non-blocking
// (returns false when idle); only read when POLLIN is set. Mirrors
// host_cinux.cpp::cinux_poll_event payload mapping (kernel Event -> EventHeader
// + Pointer/Keycode payload).
bool host_poll_event(void* ctx, EventHeader* out, uint16_t cap) {
    auto* st = static_cast<HostState*>(ctx);
    if (out == nullptr || st->ev_fd < 0 || cap < sizeof(EventHeader)) {
        return false;
    }
    struct pollfd pfd;
    pfd.fd      = st->ev_fd;
    pfd.events  = POLLIN;
    pfd.revents = 0;
    if (poll(&pfd, 1, 0) <= 0) {
        return false;  // idle
    }
    kernel_event kev;
    ssize_t      n = read(st->ev_fd, &kev, sizeof(kev));
    if (n != static_cast<ssize_t>(sizeof(kev))) {
        return false;
    }
    out->magic   = kEventMagic;
    out->version = kAbiVersion;
    out->flags   = 0;
    auto* tail   = reinterpret_cast<uint8_t*>(out) + sizeof(EventHeader);
    switch (kev.type_) {
    case kMove:
    case kMouseDown:
    case kMouseUp: {
        if (cap < sizeof(EventHeader) + sizeof(PointerPayload)) {
            return false;
        }
        out->type = EventCode::kPointer;
        PointerPayload p;
        p.kind    = (kev.type_ == kMouseDown) ? kPointerKindDown
                    : (kev.type_ == kMouseUp) ? kPointerKindUp
                                              : kPointerKindMove;
        p.x       = kev.mouse.x;
        p.y       = kev.mouse.y;
        p.dx      = kev.mouse.dx;
        p.dy      = kev.mouse.dy;
        p.buttons = kev.mouse.buttons;
        memcpy(tail, &p, sizeof(p));
        out->payload_len = sizeof(p);
        break;
    }
    case kKeyDown:
    case kKeyUp: {
        if (cap < sizeof(EventHeader) + sizeof(KeycodePayload)) {
            return false;
        }
        out->type  = EventCode::kKeycode;
        out->flags = (kev.type_ == kKeyDown) ? kEventFlagPressed : 0;
        KeycodePayload k;
        k.ascii     = kev.key.ascii;
        k.scancode  = kev.key.scancode;
        k.modifiers = static_cast<uint8_t>((kev.key.shift ? kKeymodShift : 0u) |
                                           (kev.key.ctrl ? kKeymodCtrl : 0u) |
                                           (kev.key.alt ? kKeymodAlt : 0u));
        memcpy(tail, &k, sizeof(k));
        out->payload_len = sizeof(k);
        break;
    }
    default:
        return false;
    }
    return true;
}

// L4 dispatch: apply one event to the widget tree (WM pointer / Desktop key).
void host_dispatch_event(void* ctx, const EventHeader* ev, const void* payload) {
    auto* st = static_cast<HostState*>(ctx);
    if (ev == nullptr || payload == nullptr || ev->magic != kEventMagic) {
        return;
    }
    if (ev->type == EventCode::kPointer) {
        if (ev->payload_len < sizeof(PointerPayload)) {
            return;
        }
        PointerPayload p;
        memcpy(&p, payload, sizeof(p));
        st->wm.process_pointer(p);  // cursor + drag/raise/close
    } else if (ev->type == EventCode::kKeycode) {
        if (ev->payload_len < sizeof(KeycodePayload)) {
            return;
        }
        KeycodePayload k;
        memcpy(&k, payload, sizeof(k));
        st->desktop.dispatch_key(k);
    }
}

uint32_t host_now_ms(void* /*ctx*/) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint32_t>(ts.tv_sec * 1000u + ts.tv_nsec / 1000000u);
}

void* host_alloc(void* /*ctx*/, size_t n) {
    return malloc(n);
}
void host_free(void* /*ctx*/, void* p) {
    free(p);
}
void host_log(void* /*ctx*/, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    putchar('\n');
}

}  // namespace

int main(int argc, char** argv) {
    // 1) framebuffer: open + geometry + mmap (mirrors fb_mmap_test.c).
    int fb_fd = open("/dev/fb0", O_RDWR);
    if (fb_fd < 0) {
        return 1;
    }
    struct fb_screen_info info;
    if (ioctl(fb_fd, FBIOGET_SCREENINFO, &info) != 0) {
        return 2;
    }
    size_t   sz = static_cast<size_t>(info.pitch) * static_cast<size_t>(info.height);
    uint8_t* fb =
        static_cast<uint8_t*>(mmap(nullptr, sz, PROT_READ | PROT_WRITE, MAP_SHARED, fb_fd, 0));
    if (fb == reinterpret_cast<uint8_t*>(-1)) {
        return 3;
    }

    // 2) input: /dev/event0 (optional -- smoke/prod need it; absent -> poll_event
    //    no-ops). O_RDONLY + poll(0) keeps the pump non-blocking.
    int ev_fd = open("/dev/event0", O_RDONLY);

    // 3) pump count: argv[1]; 0 = forever (production). Default 1. Parsed by
    //    hand (host glibc wraps strtoul as __isoc23_strtoul, absent from musl).
    unsigned long n_pump = 1;
    if (argc > 1 && argv[1] != nullptr) {
        const char*   s = argv[1];
        unsigned long v = 0;
        while (*s >= '0' && *s <= '9') {
            v = v * 10u + static_cast<unsigned long>(*s - '0');
            ++s;
        }
        n_pump = v;
    }

    // 4) widget tree (mirrors host_cinux.cpp::cinux_host_init, minimal: no icons).
    HostState st{};
    st.fb       = fb;
    st.fb_w     = info.width;
    st.fb_h     = info.height;
    st.fb_pitch = info.pitch;
    st.ev_fd    = ev_fd;
    st.font.init();
    st.theme = material_dark();
    st.wm.set_rect(0, 0, info.width, info.height);
    st.wm.set_theme(&st.theme);
    const int32_t wx0 = static_cast<int32_t>(info.width) / 2 - static_cast<int32_t>(kWinW) / 2;
    const int32_t wy0 = static_cast<int32_t>(info.height) / 2 - static_cast<int32_t>(kWinH) / 2;
    st.win.set_title("Cinux");
    st.win.set_theme(&st.theme);
    st.win.set_rect(wx0, wy0, kWinW, kWinH);
    st.content.set_text("Hello\nCinux-GUI");
    st.content.set_color(st.theme.on_surface);
    st.win.set_content(&st.content);
    st.win.layout();
    st.wm.add_window(&st.win);
    st.desktop.set_root(&st.wm);

    // 5) host table.
    Host host{};
    host.core.flush          = host_flush;
    host.core.poll_event     = (ev_fd >= 0) ? host_poll_event : nullptr;
    host.core.dispatch_event = (ev_fd >= 0) ? host_dispatch_event : nullptr;
    host.core.render_frame   = host_render_frame;
    host.core.now_ms         = host_now_ms;
    host.core.alloc          = host_alloc;
    host.core.free           = host_free;
    host.core.log            = host_log;
    host.desktop             = nullptr;
    host.ctx                 = &st;

    auto* core = new GuiCore(&host, info.width, info.height, PixelFormat::kXrgb8888);

    // 6) pump loop.
    unsigned long iter = (n_pump == 0) ? ~0UL : n_pump;
    for (unsigned long i = 0; i < iter; ++i) {
        core->pump();
    }

    // 7) readback smoke: center pixel non-zero (rendered bg + widget).
    int rc = 0;
    if (n_pump > 0) {
        uint32_t        cx = info.width / 2;
        uint32_t        cy = info.height / 2;
        const uint32_t* px = reinterpret_cast<const uint32_t*>(fb) +
                             static_cast<size_t>(cy) * (info.pitch / 4u) + cx;
        if (*px == 0u) {
            rc = 5;  // fb blank -- render/flush path broke
        }
    }

    delete core;
    return rc;
}
