#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include "../vst3-port/core_scc_fifo.h"
int main(){
 using C=eiii::vst3::CoreSccFifo;
 C::State s;s.a.cursors={0,1,9,0,1,4};
 s.b.cursors={1,2,3,0,0,2};
 s.a.rx_data[3]=42;s.b.tx_errors[2]=7;
 std::vector<uint8_t> wire;
 assert(C::encode(s,wire));C::State out;
 assert(C::decode(wire.data(),wire.size(),out));
 assert(out.a.rx_data[3]==42&&out.b.tx_errors[2]==7);
 wire.back()^=1;assert(!C::decode(wire.data(),wire.size(),out));
 assert(out.a.rx_data[3]==42);
 s.a.cursors[0]=9;assert(!C::encode(s,wire));
}
