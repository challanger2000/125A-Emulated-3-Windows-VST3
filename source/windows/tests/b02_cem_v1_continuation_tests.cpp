#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include "../../upstream/cem/eiii_cem3387.h"
int main(){
 eiii::cem3387_voice a,b;a.prepare(192000);b.prepare(192000);
 a.set_cv(1,13440);
 for(int i=0;i<100;++i){double l=0,r=0;a.process(int16_t(i*125),l,r);}
 const auto saved=a.capture_recall_state();
 assert(b.restore_recall_state(saved));
 for(int i=0;i<200;++i){
  double al=0,ar=0,bl=0,br=0;
  a.process(int16_t(i*100),al,ar);
  b.process(int16_t(i*100),bl,br);
  assert(al==bl&&ar==br);
 }
 auto bad=saved;bad.values[0]=NAN;
 assert(!b.restore_recall_state(bad));
}
