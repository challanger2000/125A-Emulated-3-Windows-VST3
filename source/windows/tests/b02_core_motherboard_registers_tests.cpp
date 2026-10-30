#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include "../vst3-port/core_motherboard_registers.h"
int main(){
 using C=eiii::vst3::CoreMotherboardRegisters;
 C::State s;s.dig[127]=0xabcd;s.cv[79]=0x1234;s.start[15]=0x12345678;
 s.pending[9]=true;s.n_ctrl=0x123456789abcdef0ULL;
 s.irq_state=true;
 std::vector<uint8_t> bytes;assert(C::encode(s,bytes));
 C::State out;assert(C::decode(bytes.data(),bytes.size(),out));
 assert(out.dig[127]==0xabcd&&out.cv[79]==0x1234);
 assert(out.pending[9]&&out.n_ctrl==s.n_ctrl&&out.irq_state);
 bytes.back()^=1;assert(!C::decode(bytes.data(),bytes.size(),out));
 assert(out.n_ctrl==s.n_ctrl);
}
