#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include "../vst3-port/core_scsi_target_section.h"
int main(){
 using C=eiii::vst3::CoreScsiTargetSection;
 C::State state;state.command[0]=0x28;state.sense[5]=0x91;
 state.command_size=12;state.initiator_id=7;state.buffer_size=1024;
 state.actions[1]={3,12,-18};state.queue_write=2;
 std::vector<uint8_t> wire;
 assert(C::encode(state,wire));
 C::State round;
 assert(C::decode(wire.data(),wire.size(),round));
 assert(round.command[0]==0x28&&round.sense[5]==0x91);
 assert(round.actions[1].param2==-18&&round.queue_write==2);
 wire.back()^=1;
 assert(!C::decode(wire.data(),wire.size(),round));
 assert(round.command[0]==0x28&&round.queue_write==2);
 C::State boundary=state;boundary.queue_read=32;boundary.queue_write=32;
 assert(C::encode(boundary,wire));
 C::State invalid=state;invalid.queue_write=33;
 assert(!C::encode(invalid,wire));
}
