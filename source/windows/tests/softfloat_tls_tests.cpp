// B01: production SoftFloat globals must be thread-local, including the FPU
// rounding and exception flags used by concurrent emulator workers.
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <thread>
#include "softfloat.h"
int main() {
    assert(softfloat_roundingMode==softfloat_round_near_even);
    assert(softfloat_exceptionFlags==0);
    constexpr int iterations=100000;
    std::atomic<unsigned> started{0};
    std::atomic<unsigned> failed{0};
    auto worker=[&](uint_fast8_t mode, uint_fast8_t flag) {
        // Every new thread must start from SoftFloat's initial state.
        if(softfloat_roundingMode!=softfloat_round_near_even ||
           softfloat_exceptionFlags!=0) failed.fetch_add(1);
        softfloat_roundingMode=mode;
        softfloat_exceptionFlags=flag;
        started.fetch_add(1,std::memory_order_release);
        while(started.load(std::memory_order_acquire)!=2)std::this_thread::yield();
        for(int i=0;i<iterations;++i) {
            if(softfloat_roundingMode!=mode ||
               softfloat_exceptionFlags!=flag) failed.fetch_add(1);
            if((i&511)==0)std::this_thread::yield();
        }
    };
    std::thread a(worker,softfloat_round_min,softfloat_flag_inexact);
    std::thread b(worker,softfloat_round_max,softfloat_flag_invalid);
    a.join();b.join();
    assert(failed.load()==0);
    assert(softfloat_roundingMode==softfloat_round_near_even);
    assert(softfloat_exceptionFlags==0);
    std::puts("PASS B01: real SoftFloat 3e rounding and exception states stay isolated across two engine threads");
    return 0;
}
