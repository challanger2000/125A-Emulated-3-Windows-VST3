#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include "../../upstream/cem/eiii_cem3387_v2.h"
#include "../vst3-port/core_cem_v2_section.h"
int main(){
 using C=eiii::vst3::CoreCemV2Section;
 eiii::cem3387_v2_bank a,b;
 eiii::cem_calibration cal;eiii::cem_v2_options opt;
 a.prepare(cal,192000,opt);b.prepare(cal,192000,opt);
 int16_t pcm[16]{};
 for(int i=0;i<113;++i){pcm[4]=int16_t(i*80);double l=0,r=0;a.process(pcm,l,r);}
 C::State state{},loaded{};
 assert(a.capture_recall_state(state));
 std::vector<uint8_t> wire;
 assert(C::encode(state,wire));
 assert(C::decode(wire.data(),wire.size(),loaded));
 assert(b.restore_recall_state(loaded));
 for(int i=0;i<330;++i){
  pcm[4]=int16_t(i*51);double al=0,ar=0,bl=0,br=0;
  a.process(pcm,al,ar);b.process(pcm,bl,br);
  assert(al==bl&&ar==br);
 }
 // Do not accept corrupted payloads or partially replace the target state.
 wire.back()^=1;
 assert(!C::decode(wire.data(),wire.size(),loaded));
}
