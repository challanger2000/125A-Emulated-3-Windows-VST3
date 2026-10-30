// B02: real CPU scheduler timebase primitive, ROM-free and independent.
#include "emu.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdio>

namespace {
 const device_type_impl test_cpu_kind{"125a-b02-test-cpu","125A B02 Clock Probe"};
 class ClockProbe final : public cpu_device {
 public:
  int remaining=0;
  explicit ClockProbe(machine_config const &cfg)
    : cpu_device(cfg,test_cpu_kind,"probe",nullptr,10'000'000) {
    set_icountptr(remaining);
  }
 protected:
  space_config_vector memory_space_config() const override { return {}; }
  void execute_run() override { adjust_icount(-icount()); }
 };
}
int main() {
 machine_config cfg;
 ClockProbe first(cfg), second(cfg);
 cpu_device::clock_recall_state initial{};
 assert(first.capture_clock_recall(initial));
 assert(initial.completed_cycles==0);
 first.run(31);
 assert(first.total_cycles()==31);
 cpu_device::clock_recall_state snap{};
 assert(first.capture_clock_recall(snap));
 assert(snap.completed_cycles==31);
 second.run(13);
 assert(second.total_cycles()==13);
 assert(second.restore_clock_recall(snap));
 assert(second.total_cycles()==31);
 second.run(11);
 assert(second.total_cycles()==42);
 assert(first.total_cycles()==31); // second instance never mutates first
 // A snapshot cannot be captured or installed while the CPU is executing.
 shim_sched().m_cpu_running=true;
 cpu_device::clock_recall_state rejected{};
 assert(!first.capture_clock_recall(rejected));
 assert(!second.restore_clock_recall(initial));
 shim_sched().m_cpu_running=false;
 assert(second.total_cycles()==42);
 // Normal stopped restore exactly recreates a saved timebase.
 assert(second.restore_clock_recall(initial));
 assert(second.total_cycles()==0);
 second.run(5);
 assert(second.total_cycles()==5);
 std::puts("PASS B02: stopped CPU cycle timebase roundtrip, isolation, execution-gate and continued progress");
}
