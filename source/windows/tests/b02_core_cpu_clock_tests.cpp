#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include "../vst3-port/core_cpu_clock.h"
int main(){
 using C=eiii::vst3::CoreCpuClock;
 C::State s;s.completed_cycles=0x1234567887654321ULL;s.debugger_pc=-17;
 std::vector<uint8_t> bytes;
 assert(C::encode(s,bytes));
 C::State o;
 assert(C::decode(bytes.data(),bytes.size(),o));
 assert(o.completed_cycles==s.completed_cycles&&o.debugger_pc==-17);
 bytes.back()^=1;
 assert(!C::decode(bytes.data(),bytes.size(),o));
 assert(o.completed_cycles==s.completed_cycles);
}
