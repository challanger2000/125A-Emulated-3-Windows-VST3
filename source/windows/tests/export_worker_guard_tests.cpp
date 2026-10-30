#include "../vst3-port/export_worker_guard.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <atomic>
#include <cassert>
#include <cstdio>
#include <new>
#include <stdexcept>
#include <string>

int main() {
    using eiii::vst3::finish_disk_export;
    std::atomic<bool> done{false};
    std::string result = "old result";
    int calls=0;

    finish_disk_export([&] {
        ++calls;
        return std::string("Disk image exported: /tmp/out.img");
    }, result, done);
    assert(done.load(std::memory_order_acquire));
    assert(result == "Disk image exported: /tmp/out.img" && calls==1);

    done.store(false);
    finish_disk_export([&]() -> std::string {
        ++calls;
        throw std::runtime_error("simulated disk I/O failure");
    }, result, done);
    assert(done.load(std::memory_order_acquire));
    assert(result.find("Disk export failed:") == 0 && calls==2);

    done.store(false);
    finish_disk_export([&]() -> std::string {
        ++calls;
        throw 42; // non-std exception must also remain inside worker
    }, result, done);
    assert(done.load(std::memory_order_acquire));
    assert(result.find("Disk export failed:") == 0 && calls==3);

    done.store(false);
    finish_disk_export([&]() -> std::string {
        ++calls;
        throw std::bad_alloc(); // unexpected allocation failure
    }, result, done);
    assert(done.load(std::memory_order_acquire));
    assert(result.find("Disk export failed:") == 0 && calls==4);

    done.store(false);
    finish_disk_export([&] { ++calls; return std::string("recovered"); },
                       result, done);
    assert(done.load(std::memory_order_acquire));
    assert(result=="recovered" && calls==5);
    std::puts("PASS R02: export worker catches std/nonstd/bad_alloc, always signals completion and recovers");
}
