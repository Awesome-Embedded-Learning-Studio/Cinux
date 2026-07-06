/**
 * @file user/cinux_gui_host/main.cpp
 * @brief CinuxOS userspace GUI host adapter (F-GUI-USERSPACE b3a)
 *
 * Drives the Cinux-GUI host-neutral core (21 freestanding C++ sources) as a
 * userspace process: opens /dev/fb0 (mmap display), builds a minimal widget
 * tree (WindowManager + Window + Label + Desktop), and pumps GuiCore which
 * renders into the core-owned staging buffer and flushes dirty rects to the
 * mmap'd framebuffer. The Host ABI table (core/host.hpp) is the only seam to
 * the core -- swapping host = swapping the table fill.
 *
 * Smoke mode: argv[1] = pump count (0 = forever for production). After N
 * pumps the smoke reads back the fb center pixel and exits 0 if non-zero
 * (rendered background/widget), 5 otherwise. Exit 1-3 = open/ioctl/mmap fail.
 *
 * Mirrors host/linux_fbdev_main.cpp + kernel/gui/host_cinux.cpp (ABI fill +
 * widget tree + flush row-blit). poll_event is NULL in b3a (smoke skips input
 * to avoid /dev/event0 blocking); b3b fills it with O_NONBLOCK read.
 */

#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include <sys/ioctl.h>
#include <sys/mman.h>

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

struct HostState {
    uint8_t*      fb;  // mmap'd /dev/fb0 base
    uint32_t      fb_w;
    uint32_t      fb_h;
    uint32_t      fb_pitch;
    PsfFont       font;
    Theme         theme;
    WindowManager wm;
    Window        win;
    Label         content;
    Desktop       desktop;
};

// L1 Display: flush one dirty rect from the core staging buffer to the mmap'd
// framebuffer. Mirrors host_cinux.cpp::cinux_flush (clamp negative origin +
// bounds; row-blit uint32 stores) but targets non-volatile RAM (the IoPhys VMA
// backing /dev/fb0) instead of volatile MMIO.
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

    // 2) pump count: argv[1]; 0 = forever (production). Default 1. Parsed by
    //    hand to avoid strtoul -- host glibc <stdlib.h> wraps it as the C23
    //    __isoc23_strtoul symbol, which musl libc.a does not provide.
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

    // 3) widget tree (mirrors host_cinux.cpp::cinux_host_init, minimal: no icons).
    HostState st{};
    st.fb       = fb;
    st.fb_w     = info.width;
    st.fb_h     = info.height;
    st.fb_pitch = info.pitch;
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

    // 4) host table. poll_event NULL: smoke skips input (avoid /dev/event0
    //    blocking); pump NULL-checks every callback, render+flush still run.
    Host host{};
    host.core.flush          = host_flush;
    host.core.poll_event     = nullptr;
    host.core.dispatch_event = nullptr;
    host.core.render_frame   = host_render_frame;
    host.core.now_ms         = host_now_ms;
    host.core.alloc          = host_alloc;
    host.core.free           = host_free;
    host.core.log            = host_log;
    host.desktop             = nullptr;
    host.ctx                 = &st;

    auto* core = new GuiCore(&host, info.width, info.height, PixelFormat::kXrgb8888);

    // 5) pump loop.
    unsigned long iter = (n_pump == 0) ? ~0UL : n_pump;
    for (unsigned long i = 0; i < iter; ++i) {
        core->pump();
    }

    // 6) readback smoke: center pixel must be non-zero (rendered bg + widget).
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
