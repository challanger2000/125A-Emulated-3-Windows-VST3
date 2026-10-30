#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include "../../upstream/cem/eiii_cem3387_v2.h"
int main(){
 eiii::cem_calibration cal; eiii::cem_v2_options opts;
 eiii::cem3387_v2_bank a,b;
 a.prepare(cal,192000,opts);b.prepare(cal,192000,opts);
 int16_t pcm[16]{};
 for(int i=0;i<200;++i){pcm[0]=int16_t(i*90);double l=0,r=0;a.process(pcm,l,r);}
 for(unsigned v=0;v<16;++v){eiii::cem3387_v2_voice::recall_state state{};
  assert(a.voice(v).capture_recall_state(state));
  assert(b.voice(v).restore_recall_state(state));}
 auto saved=a.capture_output_recall();
 assert(b.restore_output_recall(saved));
 for(int i=0;i<300;++i){pcm[0]=int16_t(i*30);double al=0,ar=0,bl=0,br=0;
 a.process(pcm,al,ar);b.process(pcm,bl,br);assert(al==bl&&ar==br);}
 auto invalid=saved;invalid.values[12]=NAN;
 assert(!b.restore_output_recall(invalid));
}
