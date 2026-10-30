#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include "../vst3-port/core_ncr_section.h"
int main(){
 using C=eiii::vst3::CoreNcrSection;
 C::State a;a.phase=5;a.ctrl=0x1234;a.regs[0]=0xfa;a.regs[6]=0x91;a.flags=15;
 std::vector<uint8_t> bytes;assert(C::encode(a,bytes));
 C::State restored;assert(C::decode(bytes.data(),bytes.size(),restored));
 assert(restored.phase==a.phase&&restored.ctrl==a.ctrl&&restored.regs[6]==0x91&&restored.flags==15);
 bytes.back()^=1;
 assert(!C::decode(bytes.data(),bytes.size(),restored));
 assert(restored.regs[6]==0x91);
 a.flags=16;assert(!C::encode(a,bytes));
 // Keep the NCR line restore free of command execution or IRQ callbacks.
}
