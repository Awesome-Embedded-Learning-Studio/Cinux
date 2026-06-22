/**
 * @file cgui/host/fake_host_main.cpp
 * @brief Standalone host-neutral smoke harness for the cgui core
 *
 * Drives cinux::gui::pump() with a hand-filled cgui_host table -- NO
 * kernel, NO framebuffer, NO real input. Built only when cgui/ is the build
 * root (`cmake -S cgui -B cgui/build && ctest --test-dir cgui/build`). If
 * this links and runs green under a standard hosted compiler, the cgui core
 * is provably host-neutral (zero host coupling): the same core TUs that drive
 * the Cinux kernel today drive this fake host with only the table fill swapped.
 *
 * This file is the seed of the SDL / MCU host adapters: replace the fake_*
 * callbacks with real SDL input/render/flush (or SPI-DMA flush on an MCU) and
 * the core runs unchanged. cgui body (widgets, compositor, M0-M9) is developed
 * against this in a separate project.
 *
 * NOT built inside the CinuxOS kernel tree -- the kernel has its own in-QEMU
 * tests under kernel/test/ (test_cgui_dirty / _region / _swraseter).
 */

#include <cstdint>
#include <cstdio>

#include "cgui_host.h"      // cgui_host, cgui_frame, cgui_rect, cgui_pixel_format
#include "cgui_pump.hpp"    // cinux::gui::pump
#include "cgui_region.hpp"  // cinux::gui::Rect, cinux::gui::rect_intersect, cinux::gui::Region

namespace {

// ---- recorded flush calls (the harness's pretend "display") ----
struct FlushRecord {
    int      x, y, w, h;
    uint32_t stride;
    int      format;
};
constexpr uint32_t kMaxRecorded = 8;
FlushRecord        g_flushed[kMaxRecorded];
uint32_t           g_flush_count = 0;

// ---- per-test render behaviour (set before each pump call) ----
enum class RenderMode {
    Idle,
    OneRect
};
RenderMode g_render_mode = RenderMode::Idle;
cgui_rect g_test_rect{};
uint32_t   g_stage[4 * 4];  // 4x4 staging buffer the dirty frame renders "into"

// ---- fake host callbacks (the table fill a real host swaps) ----
bool fake_poll_event(void* /*ctx*/, cgui_event_header* /*out*/, uint16_t /*out_cap*/) {
    return false;  // no input queued -- the drain loop does nothing
}

void fake_dispatch_event(void* /*ctx*/, const cgui_event_header* /*ev*/, const void* /*payload*/) {
    // no-op: nothing to apply in a fake host
}

void fake_render_frame(void* /*ctx*/, cgui_frame* frame) {
    if (g_render_mode == RenderMode::Idle) {
        frame->count = 0;  // idle: nothing changed this iteration
        return;
    }
    frame->rects[0] = g_test_rect;
    frame->count    = 1;
    frame->pixels   = g_stage;
    frame->stride   = 4 * static_cast<uint32_t>(sizeof(uint32_t));
    frame->width    = 4;
    frame->height   = 4;
    frame->format   = CGUI_PIX_XRGB8888;
}

void fake_flush(void* /*ctx*/, int x, int y, int w, int h, const void* /*pixels*/, uint32_t stride,
                cgui_pixel_format fmt) {
    if (g_flush_count < kMaxRecorded) {
        g_flushed[g_flush_count] = {x, y, w, h, stride, static_cast<int>(fmt)};
    }
    g_flush_count++;
}

cgui_host make_host() {
    cgui_host h{};
    h.core.poll_event     = fake_poll_event;
    h.core.dispatch_event = fake_dispatch_event;
    h.core.render_frame   = fake_render_frame;
    h.core.flush          = fake_flush;
    h.desktop             = nullptr;
    h.ctx                 = nullptr;
    return h;
}

void reset(RenderMode mode, cgui_rect r) {
    g_render_mode = mode;
    g_test_rect   = r;
    g_flush_count = 0;
}

#define CHECK(cond, ...)                                                                           \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            std::printf("FAIL: " __VA_ARGS__);                                                     \
            std::printf("\n");                                                                     \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

int run() {
    // 1. NULL host is a safe no-op (defensive discipline of pump).
    cinux::gui::pump(nullptr);

    // 2. Idle frame: render_frame reports count==0 -> pump flushes nothing.
    reset(RenderMode::Idle, {});
    cgui_host h = make_host();
    cinux::gui::pump(&h);
    CHECK(g_flush_count == 0, "idle frame flushed %u times (expected 0)", g_flush_count);

    // 3. Dirty frame: one rect {x0=1,y0=2,x1=3,y1=4} -> flush(x=1,y=2,w=2,h=2) once.
    reset(RenderMode::OneRect, cgui_rect{1, 2, 3, 4});
    h = make_host();
    cinux::gui::pump(&h);
    CHECK(g_flush_count == 1, "dirty frame flushed %u times (expected 1)", g_flush_count);
    CHECK(g_flushed[0].x == 1 && g_flushed[0].y == 2 && g_flushed[0].w == 2 && g_flushed[0].h == 2,
          "flush geometry {%d,%d,%d,%d} (expected 1,2,2,2)", g_flushed[0].x, g_flushed[0].y,
          g_flushed[0].w, g_flushed[0].h);
    CHECK(g_flushed[0].format == CGUI_PIX_XRGB8888, "flush format %d (expected XRGB8888)",
          g_flushed[0].format);

    // 4. Region algebra ports (pure logic, no host): intersect + bounded Region add.
    cinux::gui::Rect a{0, 0, 10, 10}, b{5, 5, 15, 15};
    cinux::gui::Rect isect = cinux::gui::rect_intersect(a, b);
    CHECK(isect.x0 == 5 && isect.y0 == 5 && isect.x1 == 10 && isect.y1 == 10,
          "rect_intersect {%d,%d,%d,%d} (expected 5,5,10,10)", isect.x0, isect.y0, isect.x1,
          isect.y1);

    cinux::gui::Region reg;
    reg.add(cinux::gui::Rect{0, 0, 4, 4});
    reg.add(cinux::gui::Rect{10, 10, 14, 14});
    CHECK(reg.count() == 2, "region count %u (expected 2)", reg.count());

    std::printf(
        "cgui_host_smoke: OK (null-host safe + idle skip + dirty flush + region "
        "algebra)\n");
    return 0;
}

}  // namespace

int main() {
    return run();
}
