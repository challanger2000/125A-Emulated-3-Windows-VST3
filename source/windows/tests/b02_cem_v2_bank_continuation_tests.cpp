#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include "../../upstream/cem/eiii_cem3387_v2.h"
int main(){
 eiii::cem_calibration cal; eiii::cem_v2_options o;
 eiii::cem3387_v2_bank a,b;a.prepare(cal,192000,o);b.prepare(cal,192000,o);
 int16_t pcm[16]{};
 for(int i=0;i<240;++i){pcm[3]=int16_t(i*40);double l=0,r=0;a.process(pcm,l,r);}
 eiii::cem3387_v2_bank::recall_state saved{};
 assert(a.capture_recall_state(saved));assert(b.restore_recall_state(saved));
 for(int i=0;i<450;++i){
  pcm[3]=int16_t(i*35);double al=0,ar=0,bl=0,br=0;
  a.process(pcm,al,ar);b.process(pcm,bl,br);
  assert(al==bl&&ar==br);
 }
 auto bad=saved;bad.output.values[12]=NAN;
 assert(!b.restore_recall_state(bad));
 assert(saved.voices[0].rng!=0);
}
