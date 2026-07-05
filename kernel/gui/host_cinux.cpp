/**
 * @file kernel/gui/host_cinux.cpp
 * @brief Cinux host adapter -- fills Host for the in-kernel desktop (F13-B)
 *
 * F13-B (2026-07-05): switched to the new Cinux-GUI core (P0-P7). render_frame
 * now drives Desktop::render over a widget tree (WindowManager + Window +
 * Label) into the CORE-owned staging buffer (the old visor-pump wm.composite()
 * + screen->back_buffer() path is gone -- Scene was retired in P6-d).
 * dispatch_event feeds PointerPayload/KeycodePayload straight to the Desktop
 * (no Event-union detour). desktop=NULL here (spawn arrives in B2 with a real
 * kernel PTY + TerminalWidget).
 *
 * poll_event / flush / now_ms / alloc / free / log are unchanged: F13 §3b/§4c
 * already aligned them to the new ABI wire format (EventHeader +
 * PointerPayload/KeycodePayload; flush takes the staging base + dirty rect).
 *
 * Compile condition: CINUX_GUI.
 */

#include "host_cinux.hpp"

#include <stdarg.h>
#include <stdint.h>

#include "kernel/drivers/mouse/mouse.hpp"
#include "kernel/drivers/pit/pit.hpp"
#include "kernel/drivers/video/framebuffer.hpp"  // Framebuffer (flush target)
#include "kernel/gui/event.hpp"                   // Event / EventType (Mouse queue element)
#include "kernel/lib/echo_trace.hpp"              // trace_char (dispatch_key diagnostic)
#include "kernel/lib/kprintf.hpp"                 // kvprintf / kprintf
#include "kernel/lib/string.hpp"                  // memcpy
#include "kernel/mm/slab.hpp"                     // kmalloc / kfree
#include "kernel/proc/sync.hpp"               // InterruptGuard (TEMP: PIT-tick probe)
#include "third_party/Cinux-GUI/core/event.hpp"
#include "third_party/Cinux-GUI/core/event_payload.hpp"
#include "third_party/Cinux-GUI/core/font.hpp"       // PsfFont
#include "third_party/Cinux-GUI/core/gui_core.hpp"   // GuiCore
#include "third_party/Cinux-GUI/core/region.hpp"     // Region (dirty rects)
#include "third_party/Cinux-GUI/core/swraster.hpp"   // Surface
#include "third_party/Cinux-GUI/core/theme.hpp"      // Theme / material_dark
#include "third_party/Cinux-GUI/core/widget.hpp"     // Desktop
#include "third_party/Cinux-GUI/core/widget/label.hpp"
#include "third_party/Cinux-GUI/core/widget/window.hpp"
#include "third_party/Cinux-GUI/core/widget/window_manager.hpp"

namespace cinux::gui {
namespace {

/* ============================================================
 * HostState: the static widget tree + core session.
 *
 * Default-constructed at static init (Widget/PsfFont default ctors are
 * side-effect-free), configured once by cinux_host_init(). Mirrors the
 * Cinux-GUI host/linux_fbdev_main.cpp HostState layout so the kernel desktop
 * exercises the SAME widget tree the standalone fbdev host does.
 * ============================================================ */
struct HostState {
    PsfFont       font{};
    Theme         theme{};
    WindowManager wm{};
    Window        win{};
    Label         content{};
    Desktop       desktop{};
};

HostState                   g_state;
Host                        g_cinux_host{};
cinux::drivers::Framebuffer* g_fb = nullptr; /* flush forwards dirty rects here */
GuiCore*                    g_core = nullptr; /* core-owned staging session  */

/* ============================================================
 * Corruption diagnosis (F13-B, temporary): font sits at HostState offset 0,
 * and paint_list_.cmds_[4096] sits at the very end of g_state, immediately
 * before g_cinux_host in BSS. A wild write crossing either boundary surfaces
 * here. We snapshot font.glyphs_ + g_cinux_host.ctx at init and compare at
 * every pump entry/exit -- the FIRST tag that reports a change localises the
 * stomping step (dispatch vs render, entry vs exit).
 * ============================================================ */
const uint8_t* g_expected_glyphs = nullptr;
void*          g_expected_ctx    = nullptr;

void check_intact(const char* tag) {
    const uint8_t* cur_glyphs = g_state.font.debug_glyphs_ptr();
    if (cur_glyphs != g_expected_glyphs) {
        cinux::lib::kprintf("[gui] %s: font.glyphs_ CORRUPT cur=%p exp=%p\n", tag,
                            static_cast<const void*>(cur_glyphs),
                            static_cast<const void*>(g_expected_glyphs));
    }
    if (g_cinux_host.ctx != g_expected_ctx) {
        cinux::lib::kprintf("[gui] %s: host.ctx CORRUPT cur=%p exp=%p\n", tag,
                            g_cinux_host.ctx, g_expected_ctx);
    }
    /* paint_list_ lives mid-g_state (cmds_[4096] ~128 KB); count_ sits just
     * past cmds_, so a wild write over the tail surfaces here. */
    const uint32_t pc = g_state.desktop.debug_paint_count();
    if (pc > 4096u) {
        cinux::lib::kprintf("[gui] %s: paint_list.count CORRUPT=%u\n", tag, pc);
    }
}

constexpr uint32_t kWinW = 200;
constexpr uint32_t kWinH = 160;

/* ============================================================
 * L2 Input: dequeue one cinux::gui::Event from the unified mouse queue and
 * serialise it to a cinux::gui EventHeader + typed payload. Unchanged from
 * F13 §3b -- already aligned to the new ABI wire format.
 * ============================================================ */
bool cinux_poll_event([[maybe_unused]] void* ctx, EventHeader* out, uint16_t out_cap) {
    if (out == nullptr || out_cap < sizeof(EventHeader)) {
        return false;
    }

    Event ev;
    if (!cinux::drivers::Mouse::event_queue().dequeue(ev)) {
        return false;
    }

    out->magic   = kEventMagic;
    out->version = kAbiVersion;
    out->flags   = 0;

    uint8_t* tail  = reinterpret_cast<uint8_t*>(out) + sizeof(EventHeader);
    uint16_t avail = static_cast<uint16_t>(out_cap - sizeof(EventHeader));

    switch (ev.type_) {
    case EventType::MouseMove:
    case EventType::MouseDown:
    case EventType::MouseUp: {
        if (avail < sizeof(PointerPayload)) {
            return false;
        }
        out->type = EventCode::kPointer;
        PointerPayload p;
        p.kind    = (ev.type_ == EventType::MouseDown) ? kPointerKindDown
                    : (ev.type_ == EventType::MouseUp) ? kPointerKindUp
                                                       : kPointerKindMove;
        p.x       = ev.mouse.x;
        p.y       = ev.mouse.y;
        p.dx      = ev.mouse.dx;
        p.dy      = ev.mouse.dy;
        p.buttons = ev.mouse.buttons;
        memcpy(tail, &p, sizeof(p));
        out->payload_len = static_cast<uint16_t>(sizeof(p));
        break;
    }
    case EventType::KeyDown:
    case EventType::KeyUp: {
        if (avail < sizeof(KeycodePayload)) {
            return false;
        }
        out->type  = EventCode::kKeycode;
        out->flags = (ev.type_ == EventType::KeyDown) ? kEventFlagPressed : 0;
        KeycodePayload k;
        k.ascii    = ev.key.ascii;
        k.scancode = ev.key.scancode;
        k.modifiers =
            static_cast<uint8_t>((ev.key.shift ? kKeymodShift : 0u) |
                                 (ev.key.ctrl ? kKeymodCtrl : 0u) | (ev.key.alt ? kKeymodAlt : 0u));
        memcpy(tail, &k, sizeof(k));
        out->payload_len = static_cast<uint16_t>(sizeof(k));
        break;
    }
    default:
        return false;
    }
    return true;
}

/* ============================================================
 * L4 dispatch: deserialise PointerPayload/KeycodePayload and feed them to the
 * widget tree via Desktop. (F13-B: was wm.handle_mouse/handle_key + Event union
 * round-trip -- the new core takes the payloads directly.)
 * ============================================================ */
void cinux_dispatch_event(void* ctx, const EventHeader* ev, const void* payload) {
    if (ev == nullptr || payload == nullptr || ev->magic != kEventMagic ||
        ev->version != kAbiVersion) {
        return;
    }

    check_intact("dispatch:entry");

    auto* st = static_cast<HostState*>(ctx);

    switch (ev->type) {
    case EventCode::kPointer: {
        if (ev->payload_len < sizeof(PointerPayload)) {
            return;
        }
        PointerPayload p;
        memcpy(&p, payload, sizeof(p));
        /* WM owns the cursor + Z-order/drag/close -- matches the fbdev host
         * (host/linux_fbdev_main.cpp). Desktop::dispatch_pointer skips the cursor
         * update (cursor_x_/y_ lives in the WM), so calling it instead of
         * process_pointer was the F13-B "no visible cursor" regression. */
        st->wm.process_pointer(p);
        break;
    }
    case EventCode::kKeycode: {
        if (ev->payload_len < sizeof(KeycodePayload)) {
            return;
        }
        KeycodePayload k;
        memcpy(&k, payload, sizeof(k));
        cinux::debug::trace_char("host.dispatch_key before desktop.dispatch_key", k.ascii);
        st->desktop.dispatch_key(k);  // route to focused widget
        break;
    }
    default:
        break;
    }
    check_intact("dispatch:exit");
}

/* ============================================================
 * L4 render: paint the widget tree into the CORE-owned staging buffer and
 * report the dirty rects. (F13-B: was wm.composite() + Terminal::poll_output
 * + screen->back_buffer() -- now a single Desktop::render call, exactly like
 * host/linux_fbdev_main.cpp.) count==0 = idle -> pump flushes nothing.
 * ============================================================ */
void cinux_render_frame(void* ctx, Frame* frame) {
    if (frame == nullptr) {
        return;
    }
    check_intact("render:entry");
    /* First-few-frames dump: staging vs g_state (BSS) overlap, Surface stride,
     * and every font field. Compositor handlers receive pixel-like garbage
     * while font.glyphs_ stays valid -- so the culprit is a wild pointer in the
     * Surface stack object, a stomped paint_list cmd, or a font field other
     * than glyphs_ (e.g. bytes_per_glyph_). */
    static int s_render_n = 0;
    if (s_render_n < 10) {
        ++s_render_n;
        const uint8_t* px         = static_cast<const uint8_t*>(frame->pixels);
        const size_t   staging_sz = static_cast<size_t>(frame->stride) * frame->height;
        const uint8_t* gs         = reinterpret_cast<const uint8_t*>(&g_state);
        const bool     overlap    = (px < gs + sizeof(HostState)) && (px + staging_sz > gs);
        cinux::lib::kprintf(
            "[gui] render#%d staging=[%p..%p) sz=%zu | g_state=[%p..%p) sz=%zu | OVERLAP=%d | "
            "frame.stride=%u | font glyphs=%p w=%u h=%u bpg=%u num=%u\n",
            s_render_n, static_cast<const void*>(px), static_cast<const void*>(px + staging_sz),
            staging_sz, static_cast<const void*>(gs),
            static_cast<const void*>(gs + sizeof(HostState)), sizeof(HostState), overlap ? 1 : 0,
            static_cast<uint32_t>(frame->stride),
            static_cast<const void*>(g_state.font.debug_glyphs_ptr()), g_state.font.width(),
            g_state.font.height(), g_state.font.bytes_per_glyph(), g_state.font.num_glyphs());
    }
    auto*   st = static_cast<HostState*>(ctx);
    Surface s{frame->pixels, frame->width, frame->height, frame->stride, frame->format};
    /* Per-frame Surface sanity: s lives on render_frame's stack and is passed by
     * reference down to fill_rect/glyph_blit. If s.pixels or stride gets stomped
     * mid-render the swraster writes a non-canonical address -> #GP. Snapshot the
     * staging pointer on frame 1, then verify it (and stride) every frame. */
    static const void* g_expected_staging = nullptr;
    if (g_expected_staging == nullptr) {
        g_expected_staging = s.pixels;
    } else if (s.pixels != g_expected_staging || s.stride_bytes != 4096u) {
        cinux::lib::kprintf("[gui] render: Surface CORRUPT s.pixels=%p(exp %p) stride=%u(exp 4096) "
                            "frame.pixels=%p stride=%u\n",
                            s.pixels, g_expected_staging, s.stride_bytes, frame->pixels, frame->stride);
    }
    Region  dirty;
    {
        /* TEMP probe: if disabling IRQs around desktop.render stops the crash,
         * a PIT tick (100 Hz) was stomping the gui_worker stack mid-render. */
        cinux::proc::InterruptGuard ig;
        st->desktop.render(s, st->font, &dirty);  // widget tree -> staging
    }
    check_intact("render:after-desktop-render");
    const uint32_t n = dirty.count();
    for (uint32_t i = 0u; i < n && i < frame->max_rects; ++i) {
        frame->rects[i] = dirty.rects()[i];
    }
    frame->count = n;
    check_intact("render:exit");
}

uint32_t cinux_now_ms([[maybe_unused]] void* ctx) {
    return static_cast<uint32_t>(cinux::drivers::PIT::get_uptime_ms());
}

/* ============================================================
 * Memory / log.
 * ============================================================ */
void* cinux_alloc([[maybe_unused]] void* ctx, size_t n) {
    return cinux::mm::kmalloc(n);
}

void cinux_free([[maybe_unused]] void* ctx, void* p) {
    cinux::mm::kfree(p);
}

__attribute__((format(printf, 2, 3))) void cinux_log([[maybe_unused]] void* ctx, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    cinux::lib::kvprintf(fmt, ap);
    va_end(ap);
}

/* ============================================================
 * L1 Display: flush a dirty rect from the staging buffer to the framebuffer.
 *
 * The core owns the staging buffer (allocated in GuiCore) and pushes only the
 * dirty rects through this callback. @p pixels is the staging buffer base; the
 * rect at display coords (x,y,w,h) lives at row offset y*stride + x*4 within
 * it. We copy each row into the VBE framebuffer (volatile MMIO), respecting
 * its own pitch. Identical algorithm to the old §4c flush -- only the staging
 * owner changed (core now, host adapter before).
 * ============================================================ */
void cinux_flush([[maybe_unused]] void* ctx, int x, int y, int w, int h, const void* pixels,
                 uint32_t stride, PixelFormat fmt) {
    if (g_fb == nullptr || pixels == nullptr || w <= 0 || h <= 0) {
        return;
    }
    if (fmt != PixelFormat::kXrgb8888) {
        return; /* Desktop framebuffer is 32bpp XRGB; other formats arrive later */
    }

    const uint32_t fb_pitch = g_fb->pitch();
    const uint32_t fb_w     = g_fb->width();
    const uint32_t fb_h     = g_fb->height();
    if (w > static_cast<int>(fb_w)) {
        w = static_cast<int>(fb_w);
    }
    if (h > static_cast<int>(fb_h)) {
        h = static_cast<int>(fb_h);
    }
    /* XRGB8888: every row transfers an integral number of uint32 stores. See
     * the old §4c flush comment for why we widen to uint32 stores by hand. */
    const uint32_t     src_stride_px = stride / 4u;
    const uint32_t     dst_stride_px = fb_pitch / 4u;
    const uint32_t     px_w          = static_cast<uint32_t>(w);
    const uint32_t*    src           = static_cast<const uint32_t*>(pixels);
    volatile uint32_t* dst           = g_fb->data();

    for (int row = 0; row < h; row++) {
        const int py = y + row;
        if (py < 0 || static_cast<uint32_t>(py) >= fb_h || x < 0 ||
            static_cast<uint32_t>(x) >= fb_w) {
            continue;
        }
        const uint32_t cols_px =
            (static_cast<uint32_t>(x) + px_w <= fb_w) ? px_w : (fb_w - static_cast<uint32_t>(x));
        const uint32_t* srow =
            src + static_cast<size_t>(py) * src_stride_px + static_cast<uint32_t>(x);
        volatile uint32_t* drow =
            dst + static_cast<size_t>(py) * dst_stride_px + static_cast<uint32_t>(x);
        for (uint32_t p = 0; p < cols_px; p++) {
            drow[p] = srow[p];
        }
    }
}

}  // namespace

Host& cinux_host() {
    return g_cinux_host;
}

GuiCore& cinux_core() {
    return *g_core;
}

void cinux_host_init(cinux::drivers::Framebuffer* fb) {
    g_fb = fb;

    /* Widget tree (mirrors Cinux-GUI host/linux_fbdev_main.cpp): a centered
     * titled Window hosting a Label, rooted at a full-screen WindowManager.
     * The new core PsfFont carries its own PSF2 data (font_psf_data.hpp). */
    g_state.font.init();
    g_state.theme = material_dark();

    const uint32_t fb_w = (fb != nullptr) ? fb->width() : 1024u;
    const uint32_t fb_h = (fb != nullptr) ? fb->height() : 768u;

    g_state.wm.set_rect(0, 0, fb_w, fb_h);
    g_state.wm.set_theme(&g_state.theme);

    const int32_t wx0 = static_cast<int32_t>(fb_w) / 2 - static_cast<int32_t>(kWinW) / 2;
    const int32_t wy0 = static_cast<int32_t>(fb_h) / 2 - static_cast<int32_t>(kWinH) / 2;
    g_state.win.set_title("Cinux");
    g_state.win.set_theme(&g_state.theme);
    g_state.win.set_rect(wx0, wy0, kWinW, kWinH);
    g_state.content.set_text("Hello\nCinux-GUI");
    g_state.content.set_color(g_state.theme.on_surface);
    g_state.win.set_content(&g_state.content);
    g_state.win.layout();
    g_state.wm.add_window(&g_state.win);
    g_state.desktop.set_root(&g_state.wm);

    /* Mouse screen bounds (was gui_start's job; the host adapter owns display
     * geometry now that the old WindowManager singleton is gone). */
    cinux::drivers::Mouse::set_screen_bounds(fb_w, fb_h);

    /* Fill the host table. desktop=NULL in B1 -- spawn arrives in B2 with a
     * real kernel PTY + TerminalWidget (HostDesktop::spawn signature). */
    g_cinux_host.core.poll_event     = cinux_poll_event;
    g_cinux_host.core.dispatch_event = cinux_dispatch_event;
    g_cinux_host.core.render_frame   = cinux_render_frame;
    g_cinux_host.core.flush          = cinux_flush;
    g_cinux_host.core.flush_complete = nullptr; /* Desktop uses sync flush */
    g_cinux_host.core.now_ms         = cinux_now_ms;
    g_cinux_host.core.alloc          = cinux_alloc;
    g_cinux_host.core.free           = cinux_free;
    g_cinux_host.core.log            = cinux_log;
    g_cinux_host.desktop             = nullptr;
    g_cinux_host.ctx                 = &g_state;

    /* Snapshot intact state for corruption diagnosis (see check_intact). */
    g_expected_glyphs = g_state.font.debug_glyphs_ptr();
    g_expected_ctx    = g_cinux_host.ctx;

    /* Construct the GuiCore over the host table + display geometry. The core
     * allocates the staging buffer (w*h*4) via global new -> kmalloc (crt_stub
     * redirects operator new to kmalloc). */
    g_core = new GuiCore(&g_cinux_host, fb_w, fb_h, PixelFormat::kXrgb8888);

    cinux::lib::kprintf("[gui] Cinux host adapter initialised (F13-B widget tree, new core)\n");
}

}  // namespace cinux::gui
