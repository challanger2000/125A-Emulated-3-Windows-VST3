#pragma once
// The export path must not read the asynchronously published HD cache before
// the owner worker has acknowledged the most recent disk writes.
#include <string>
#include <utility>

namespace eiii::vst3 {
template <class Flush, class Read>
bool capture_disk_export_overlay(Flush&& flush, Read&& read, std::string& overlay) {
    overlay.clear();
    if (!std::forward<Flush>(flush)()) return false;
    overlay = std::forward<Read>(read)();
    return true;
}
} // namespace eiii::vst3
