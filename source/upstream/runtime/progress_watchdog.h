// Host-side liveness policy. Does not schedule events or change device timing.
#pragma once
#include <cstdint>

namespace eiii {
class progress_watchdog {
public:
    static constexpr unsigned stall_limit=10000; // existing runtime limit
    bool observe(uint64_t cpu_cycles,long double scheduler_seconds) noexcept {
        // A long, completed CPU instruction can leave overdue periodic timers.
        // Dispatching those in time order is progress even while CPU time stays
        // fixed. A backwards or unchanged scheduler clock is not progress.
        if(cpu_cycles!=last_cpu_ || scheduler_seconds>last_scheduler_) {
            last_cpu_=cpu_cycles;last_scheduler_=scheduler_seconds;stalled_=0;
            return false;
        }
        if(stalled_<stall_limit)++stalled_;
        return stalled_==stall_limit;
    }
    unsigned stalled_steps() const noexcept {return stalled_;}
private:
    uint64_t last_cpu_=0;
    long double last_scheduler_=0;
    unsigned stalled_=0;
};
}
