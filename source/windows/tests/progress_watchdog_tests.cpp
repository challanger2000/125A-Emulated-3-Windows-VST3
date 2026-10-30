#include "../../upstream/runtime/progress_watchdog.h"
#include <iostream>
#include <stdexcept>

void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
int main() {
    using eiii::progress_watchdog;
    // Measured OS 2.42 MOVSW: CPU jumps 95678900 -> 99027250, then
    // catches up >10000 overdue 32-us MIDI deadlines without CPU execution.
    progress_watchdog catchup;
    require(!catchup.observe(95678900,9.567872L),"initial progress");
    require(!catchup.observe(99027250,9.567904L),"completed MOVSW");
    unsigned cpu_only_stalls=0;
    for(uint64_t deadline=95679360;deadline<=99027250;deadline+=320) {
        ++cpu_only_stalls;
        require(!catchup.observe(99027250,static_cast<long double>(deadline)/10000000),"timer catch-up falsely stopped");
        require(catchup.stalled_steps()==0,"timer progress counted as a stall");
    }
    require(cpu_only_stalls>=progress_watchdog::stall_limit,"old false-positive control not reached");
    require(cpu_only_stalls==10463,"measured catch-up interval changed");
    require(!catchup.observe(99027251,9.902720L),"CPU resumes after catch-up");
    // Actual non-progress still trips at exactly the original limit, even
    // after a successful timer catch-up. Checks remain active under NDEBUG.
    for(unsigned i=1;i<progress_watchdog::stall_limit;++i)
        require(!catchup.observe(99027251,9.902720L),"watchdog trips too early");
    require(catchup.observe(99027251,9.902720L),"real stall not detected");
    require(catchup.stalled_steps()==10000,"stall limit changed");
    require(catchup.observe(99027251,9.902720L),"stall counter wrapped");
    require(!catchup.observe(99027252,9.902720L),"CPU progress did not reset watchdog");
    progress_watchdog backwards;backwards.observe(100,1.L);
    for(unsigned i=1;i<progress_watchdog::stall_limit;++i)
        require(!backwards.observe(100,i%2?.5L:1.L),"backwards-clock control trips early");
    require(backwards.observe(100,1.L),"backwards/repeated timer times disguised a stall");
    std::cout<<"PASS scheduler progress: 10463 overdue MIDI ticks after first callback; unchanged/backwards clocks still fail at 10000\n";
}
