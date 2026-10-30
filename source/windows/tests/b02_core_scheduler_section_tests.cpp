#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include "../vst3-port/core_scheduler_section.h"
int main(){
 using C=eiii::vst3::CoreSchedulerSection;
 C::State s;s.basetime=0.25;
 s.timers={{0.5,0.25,0.1,17,true},{1e30,0.25,1e30,-5,false}};
 std::vector<uint8_t> wire;assert(C::encode(s,wire));
 C::State copy;assert(C::decode(wire.data(),wire.size(),copy));
 assert(copy.basetime==s.basetime&&copy.timers.size()==2);
 assert(copy.timers[0].param==17&&copy.timers[1].expire==1e30);
 wire.back()^=1;
 assert(!C::decode(wire.data(),wire.size(),copy));
 assert(copy.timers.size()==2);
 C::State bad=s;bad.timers[0].start=-1;
 assert(!C::encode(bad,wire));
 // Only valid stopped-thread timer snapshots are eligible for machine recall.
}
