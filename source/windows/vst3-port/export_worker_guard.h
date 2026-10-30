#pragma once
// R02: contain exceptions inside the disk-export worker, before std::thread exits.
// Completion is published with release semantics even on unexpected I/O or
// allocation failures, so the UI can join the worker and display a failure.
#include <atomic>
#include <exception>
#include <string>
#include <utility>

namespace eiii::vst3 {
template <class Work>
void finish_disk_export(Work&& work, std::string& result,
                        std::atomic<bool>& finished) noexcept {
    std::string message;
    try {
        message = std::forward<Work>(work)();
    } catch (const std::exception&) {
        try { message = "Disk export failed: unexpected internal error"; }
        catch (...) { message.clear(); }
    } catch (...) {
        try { message = "Disk export failed: unexpected internal error"; }
        catch (...) { message.clear(); }
    }
    result.swap(message); // noexcept for std::string's standard allocator
    finished.store(true, std::memory_order_release);
}
} // namespace eiii::vst3
