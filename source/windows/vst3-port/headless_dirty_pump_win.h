#pragma once
// R04: editor-independent, UI-thread-only periodic callback on Windows.
// Uses a message-only built-in STATIC window; no unloadable Win32 class or
// thread-timer callback remains registered after the processor terminates.
// Never create this from the audio/emulator worker thread.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <utility>

namespace eiii::vst3 {
class HeadlessDirtyPump final {
public:
    using Callback = void (*)(void*);
    HeadlessDirtyPump() = default;
    HeadlessDirtyPump(const HeadlessDirtyPump&) = delete;
    HeadlessDirtyPump& operator=(const HeadlessDirtyPump&) = delete;
    ~HeadlessDirtyPump() { stop(); }

    bool start(void* context, Callback callback, UINT intervalMs = 250) noexcept {
        if (window_ || !context || !callback || !intervalMs) return false;
        HWND window = CreateWindowExW(0, L"STATIC", L"125A EIII Dirty Pump",
                                     0, 0, 0, 0, 0, HWND_MESSAGE, nullptr,
                                     GetModuleHandleW(nullptr), nullptr);
        if (!window) return false;
        callback_ = callback;
        context_ = context;
        ownerThread_ = GetCurrentThreadId();
        SetLastError(0);
        const LONG_PTR previous = SetWindowLongPtrW(
            window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&window_proc));
        if (!previous && GetLastError()!=0) {
            DestroyWindow(window);
            clear();
            return false;
        }
        previousProc_ = reinterpret_cast<WNDPROC>(previous);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
        window_ = window;
        if (!SetTimer(window, kTimerId, intervalMs, nullptr)) {
            stop();
            return false;
        }
        return true;
    }

    void stop() noexcept {
        if (!window_) return;
        // VST3 initialize/terminate run on the UI thread. DestroyWindow and
        // WNDPROC restoration require this owning thread; never wait/block.
        if (GetCurrentThreadId() != ownerThread_) {
            OutputDebugStringW(L"125A EIII: headless pump stopped from wrong thread");
            return;
        }
        HWND window = std::exchange(window_, nullptr);
        KillTimer(window, kTimerId);
        SetWindowLongPtrW(window, GWLP_WNDPROC,
                          reinterpret_cast<LONG_PTR>(previousProc_));
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        DestroyWindow(window);
        clear();
    }

    bool running() const noexcept { return window_ != nullptr; }

private:
    static constexpr UINT_PTR kTimerId = 125;
    HWND window_ = nullptr;
    WNDPROC previousProc_ = nullptr;
    DWORD ownerThread_ = 0;
    Callback callback_ = nullptr;
    void* context_ = nullptr;

    void clear() noexcept {
        previousProc_ = nullptr;
        ownerThread_ = 0;
        callback_ = nullptr;
        context_ = nullptr;
    }

    static LRESULT CALLBACK window_proc(HWND window, UINT message,
                                        WPARAM wParam, LPARAM lParam) noexcept {
        auto* self = reinterpret_cast<HeadlessDirtyPump*>(
            GetWindowLongPtrW(window, GWLP_USERDATA));
        if (!self) return DefWindowProcW(window, message, wParam, lParam);
        if (message==WM_TIMER && wParam==kTimerId) {
            // Never allow a C++ exception to unwind through a Win32 callback.
            try { if (self->callback_) self->callback_(self->context_); }
            catch (...) {}
            return 0;
        }
        return self->previousProc_
            ? CallWindowProcW(self->previousProc_, window, message, wParam, lParam)
            : DefWindowProcW(window, message, wParam, lParam);
    }
};
} // namespace eiii::vst3
