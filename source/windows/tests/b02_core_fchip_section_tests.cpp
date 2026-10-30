#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include "../vst3-port/core_fchip_section.h"
int main(){
 using C=eiii::vst3::CoreFchipSection;
 C::State state{},roundtrip{};
 state[2].rf={1,2,3,4};state[2].phase=123;
 state[2].rate=4095;state[2].active=true;
 state[2].ctrl_phase=1;state[2].ctrl_hi=0xf3;
 std::vector<uint8_t> bytes;
 assert(C::encode(state,bytes));
 assert(C::decode(bytes.data(),bytes.size(),roundtrip));
 assert(roundtrip[2].rf[3]==4&&roundtrip[2].phase==123);
 assert(roundtrip[2].rate==4095&&roundtrip[2].ctrl_hi==0xf3);
 bytes.back()^=1;
 assert(!C::decode(bytes.data(),bytes.size(),roundtrip));
 assert(roundtrip[2].phase==123);
}
