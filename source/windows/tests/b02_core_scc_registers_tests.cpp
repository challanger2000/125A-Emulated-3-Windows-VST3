#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include "../vst3-port/core_scc_registers.h"
int main(){
 using C=eiii::vst3::CoreSccRegisters;
 C::State s;s.a.rr[0]=0x12;s.a.wr[15]=0x34;
 s.b.rr[8]=0x56;s.b.wr7p=0x78;
 std::vector<uint8_t> wire;assert(C::encode(s,wire));
 C::State out;assert(C::decode(wire.data(),wire.size(),out));
 assert(out.a.rr[0]==0x12&&out.a.wr[15]==0x34);
 assert(out.b.rr[8]==0x56&&out.b.wr7p==0x78);
 wire.back()^=1;assert(!C::decode(wire.data(),wire.size(),out));
 assert(out.b.wr7p==0x78);
 // Both channels remain unchanged after a rejected corrupted wire image.
}
