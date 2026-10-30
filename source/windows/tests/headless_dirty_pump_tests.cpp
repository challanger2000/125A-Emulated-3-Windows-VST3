// R04: an editorless instance gets callbacks on its owning Windows UI thread.
// No Steinberg SDK, firmware, editor HWND or audio process callback required.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "../vst3-port/headless_dirty_pump_win.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdio>
#include <stdexcept>

struct Counter {
    DWORD owner = GetCurrentThreadId();
    int ticks = 0;
    bool throwFirst = false;
};
static void tick(void* ptr) {
    auto& c = *static_cast<Counter*>(ptr);
    assert(GetCurrentThreadId()==c.owner);
    ++c.ticks;
    if (c.throwFirst && c.ticks == 1)
        throw std::runtime_error("simulated error; must not escape Win32 WndProc");
}
static bool pump_until(Counter& c, int minimum, DWORD timeoutMs = 1400) {
    const auto started = GetTickCount64();
    while (c.ticks < minimum && GetTickCount64()-started < timeoutMs) {
        MSG msg{};
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        Sleep(3);
    }
    return c.ticks >= minimum;
}
int main() {
    using eiii::vst3::HeadlessDirtyPump;
    Counter a{}, b{};
    a.throwFirst = true;
    HeadlessDirtyPump first, second;
    assert(!first.start(nullptr, tick, 15));
    assert(!first.running());
    assert(first.start(&a, tick, 15));
    assert(!first.start(&a, tick, 15)); // no double timer
    assert(second.start(&b, tick, 15));  // multiple separate instances
    assert(pump_until(a, 3) && pump_until(b, 2));
    first.stop();
    first.stop(); // idempotent
    assert(!first.running() && second.running());
    const int stoppedCount = a.ticks;
    assert(pump_until(b, b.ticks+3));
    assert(a.ticks == stoppedCount); // no stale callback into destroyed view
    assert(first.start(&a, tick, 15)); // restart on same UI thread
    assert(pump_until(a, stoppedCount+2));
    first.stop();
    second.stop();
    assert(!first.running() && !second.running());
    std::puts("PASS R04: headless UI timer, multiple plugin instances, exception fence, shutdown/restart");
    return 0;
}
