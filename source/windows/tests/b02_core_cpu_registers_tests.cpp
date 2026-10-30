#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include "../vst3-port/core_cpu_registers.h"
int main(){
 using C=eiii::vst3::CoreCpuRegisters;
 C::State s; s.prefix={{0x1234,0xabcdef,1,2,3,4,5,6,0}};
 s.r[0]=0xdeadbeef;s.r[7]=0x98765432;s.sps=1;s.psr=0xaabb;s.mod=0xccdd;s.lines=31;
 std::vector<uint8_t> bytes; assert(C::encode(s,bytes));
 C::State restored;assert(C::decode(bytes.data(),bytes.size(),restored));
 assert(restored.r==s.r&&restored.prefix==s.prefix&&restored.mod==s.mod&&restored.lines==31);
 bytes.back()^=1;
 assert(!C::decode(bytes.data(),bytes.size(),restored));
 assert(restored.r==s.r&&restored.psr==s.psr);
 bytes.back()^=1;bytes[20]^=1;
 assert(!C::decode(bytes.data(),bytes.size(),restored));
 assert(restored.prefix==s.prefix);
 // All registers are staged only after the complete verified envelope is decoded.
}
