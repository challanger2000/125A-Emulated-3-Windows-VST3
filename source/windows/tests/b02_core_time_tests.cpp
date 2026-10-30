#ifdef NDEBUG
#undef NDEBUG
#endif
#include "../vst3-port/core_scheduler_section.h"
#include <cassert>
#include <limits>
int main(){
 using namespace eiii::vst3;
 for(long double time:{0.L,17.L/10000000.L,12345.L/31250.L,static_cast<long double>(1e30)}){
  std::vector<uint8_t> bytes;long double restored=-1;
  assert(CoreTime::put(bytes,time));assert(CoreTime::get(bytes.data(),restored));
  assert(time==restored); // no epsilon: preserving native emulation precision
 }
 std::vector<uint8_t> bytes;
 assert(!CoreTime::put(bytes,-1));
 assert(!CoreTime::put(bytes,std::numeric_limits<long double>::quiet_NaN()));
 CoreSchedulerSection::State state;state.basetime=17.L/10000000.L;
 state.timers={{1e30,0,1e30,0,true}}; // real serial adjust(never)
 assert(CoreSchedulerSection::encode(state,bytes));
 CoreSchedulerSection::State restored;
 assert(CoreSchedulerSection::decode(bytes.data(),bytes.size(),restored));
 assert(restored.basetime==state.basetime&&restored.timers[0].enabled);
 // Preserve the old binary64 scheduler format as well.
 std::vector<uint8_t> body;CoreStateEnvelope::put32(body,CoreSchedulerSection::legacy_tag);
 CoreStateEnvelope::put32(body,0);CoreTime::put_double(body,.125);
 assert(CoreStateEnvelope::encode(body,bytes));
 assert(CoreSchedulerSection::decode(bytes.data(),bytes.size(),restored));
 assert(restored.basetime==.125L&&restored.timers.empty());
}
