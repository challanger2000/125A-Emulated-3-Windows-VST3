#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include "../vst3-port/core_cem_v1_section.h"
int main(){
 using C=eiii::vst3::CoreCemV1Section;
 C::State state{},restored{};
 for(auto& v:state)v.values[0]=192000;
 state[3].values[7]=2.5;
 state[3].keys[2]=0xfedcba9876543210ULL;
 std::vector<uint8_t> wire;
 assert(C::encode(state,wire));
 assert(C::decode(wire.data(),wire.size(),restored));
 assert(restored[3].values[7]==2.5);
 assert(restored[3].keys[2]==0xfedcba9876543210ULL);
 auto broken=wire;broken.back()^=1;
 assert(!C::decode(broken.data(),broken.size(),restored));
 assert(restored[3].values[7]==2.5);
 state[3].values[7]=NAN;
 assert(!C::encode(state,wire));
}
