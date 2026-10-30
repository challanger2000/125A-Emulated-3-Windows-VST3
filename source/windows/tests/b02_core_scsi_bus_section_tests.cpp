#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include "../vst3-port/core_scsi_bus_section.h"
int main(){
 using C=eiii::vst3::CoreScsiBusSection;
 C::State a;a.ctrl=0x10;a.data=0x55;a.devices={{"disk1",1,2,3},{"cd3",4,5,6}};
 std::vector<uint8_t> wire;assert(C::encode(a,wire));
 C::State restored;assert(C::decode(wire.data(),wire.size(),restored));
 assert(restored.devices.size()==2&&restored.devices[0].tag=="disk1");
 assert(restored.devices[1].wait_ctrl==5&&restored.ctrl==0x10);
 wire.back()^=1;
 assert(!C::decode(wire.data(),wire.size(),restored));
 assert(restored.devices[0].tag=="disk1");
 // Verify no partially decoded roster escapes on checksum failure.
}
