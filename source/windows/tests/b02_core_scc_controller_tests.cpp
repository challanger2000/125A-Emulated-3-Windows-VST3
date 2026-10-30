#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include "../vst3-port/core_scc_controller.h"
int main(){
 using C=eiii::vst3::CoreSccController;
 C::State s;s.wr9=0x80;s.wr0_ptrbits=7;s.out_int_state=1;
 s.int_state[3]=2;s.int_source[5]=3;
 std::vector<uint8_t> bytes;
 assert(C::encode(s,bytes));
 C::State out;
 assert(C::decode(bytes.data(),bytes.size(),out));
 assert(out.wr9==0x80&&out.int_state[3]==2&&out.int_source[5]==3);
 bytes.back()^=1;
 assert(!C::decode(bytes.data(),bytes.size(),out));
 assert(out.int_state[3]==2);
 s.int_source[5]=4;
 assert(!C::encode(s,bytes));
}
