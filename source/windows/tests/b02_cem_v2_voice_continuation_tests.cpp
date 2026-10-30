#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include "../../upstream/cem/eiii_cem3387_v2.h"
int main(){
 eiii::cem_params p;
 eiii::cem_v2_options o;
 eiii::cem3387_v2_voice a,b;
 a.prepare(p,192000,o,8127);
 b.prepare(p,192000,o,8127);
 a.set_word(1,12000);
 a.set_word(3,14000);
 for(int i=0;i<150;++i){double l=0,r=0;a.process(int16_t(i*60),l,r);}
 eiii::cem3387_v2_voice::recall_state snapshot{};
 assert(a.capture_recall_state(snapshot));
 assert(b.restore_recall_state(snapshot));
 for(int i=0;i<400;++i){
  double al=0,ar=0,bl=0,br=0;
  a.process(int16_t(i*30),al,ar);
  b.process(int16_t(i*30),bl,br);
  assert(al==bl&&ar==br);
 }
 auto bad=snapshot;bad.values[0]=NAN;
 assert(!b.restore_recall_state(bad));
 assert(snapshot.rng!=0);
}
