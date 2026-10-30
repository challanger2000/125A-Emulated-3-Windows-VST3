#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <vector>
#include "../vst3-port/core_fpu_section.h"
int main(){
 using C=eiii::vst3::CoreFpuSection;
 C::State a;a.fsr=0x1234;a.state=4;a.tcy=99;a.idbyte=0xAB;
 a.opword=0x3456;a.status=0x8765;a.op[0]={4,2,0xFEDCBA9876543210ULL};
 a.f[7]=0xdeadbeef;
 std::vector<uint8_t> wire;assert(C::encode(a,wire));
 C::State b;assert(C::decode(wire.data(),wire.size(),b));
 assert(b.fsr==a.fsr&&b.op[0].value==a.op[0].value);
 assert(b.f[7]==a.f[7]&&b.status==a.status);
 wire.back()^=1;
 assert(!C::decode(wire.data(),wire.size(),b));
 assert(b.f[7]==a.f[7]);
 C::State bad=a;bad.op[0].issued=5;
 assert(!C::encode(bad,wire));
 // Guard in-flight operation progress before handing state to the emulator.
}
