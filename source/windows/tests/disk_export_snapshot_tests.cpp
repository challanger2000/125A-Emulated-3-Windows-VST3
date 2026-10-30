#include "../vst3-port/disk_export_snapshot.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdio>
#include <string>

int main() {
    using eiii::vst3::capture_disk_export_overlay;
    std::string snapshot = "stale";
    int step = 0;
    // Without a worker acknowledgement, even a previously cached snapshot
    // must not be exported; it must never be read from the cache.
    const bool timed_out = capture_disk_export_overlay(
        [&] { assert(++step == 1); return false; },
        [&] { assert(false && "cache read after failed flush"); return std::string("stale"); },
        snapshot);
    assert(!timed_out && snapshot.empty() && step == 1);

    step = 0;
    const bool ready = capture_disk_export_overlay(
        [&] { assert(++step == 1); return true; },
        [&] { assert(++step == 2); return std::string("new-write"); },
        snapshot);
    assert(ready && step == 2 && snapshot == "new-write");

    // For an inactive engine the published final cache may be read, as
    // no active worker can be asked to flush again.
    step = 0;
    const bool stopped = capture_disk_export_overlay(
        [&] { assert(++step == 1); return true; },
        [&] { assert(++step == 2); return std::string("final-cache"); },
        snapshot);
    assert(stopped && step == 2 && snapshot == "final-cache");
    std::puts("PASS: disk export requires flush acknowledgement before reading HD changes");
}
