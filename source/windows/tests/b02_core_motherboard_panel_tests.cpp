#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include "../vst3-port/core_motherboard_panel.h"
int main(){
 using T=eiii::vst3::CoreMotherboardPanel;
 T::State s;s.leds=0x1234;s.scan_head=14;s.scan_count=3;s.scan_tail=1;
 s.scan_fifo[15]=0x42;s.adc_latch[0]=-123;s.adc_pot[7]=65535;
 std::vector<uint8_t> bytes;assert(T::encode(s,bytes));
 T::State t;assert(T::decode(bytes.data(),bytes.size(),t));
 assert(t.leds==s.leds&&t.scan_tail==1&&t.scan_fifo[15]==0x42);
 assert(t.adc_latch[0]==-123&&t.adc_pot[7]==65535);
 bytes.back()^=1;assert(!T::decode(bytes.data(),bytes.size(),t));
 assert(t.scan_count==3);
 s.scan_tail=2;assert(!T::encode(s,bytes));
}
