#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include "../vst3-port/core_motherboard_lcd.h"
int main(){
 using C=eiii::vst3::CoreMotherboardLcd;
 C::State s;s.ddram[0]=0x41;s.cgram[63]=0x42;s.ac=17;s.dir=-1;s.blink_on=true;
 std::vector<uint8_t> wire;assert(C::encode(s,wire));
 C::State out;assert(C::decode(wire.data(),wire.size(),out));
 assert(out.ddram[0]==0x41&&out.cgram[63]==0x42&&out.ac==17&&out.dir==-1&&out.blink_on);
 wire.back()^=1;assert(!C::decode(wire.data(),wire.size(),out));
 assert(out.ac==17);
 s.ac=128;assert(!C::encode(s,wire));
}
