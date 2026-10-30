#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include "../../upstream/cem/eiii_cem_cv_conditioning.h"
int main(){
 eiii::cem_params p;
 eiii::cem_cv_conditioning a,b;
 a.prepare(p,192000);b.prepare(p,192000);
 a.set_word(3,13000);
 for(unsigned i=0;i<177;++i)a.step();
 eiii::cem_cv_conditioning::recall_state state{};
 assert(a.capture_recall_state(state));
 assert(b.restore_recall_state(state));
 for(unsigned i=0;i<500;++i){
  a.step();b.step();
  for(unsigned j=0;j<5;++j)assert(a.pin(j)==b.pin(j));
 }
 auto bad=state;bad.scalars[1]=NAN;
 assert(!b.restore_recall_state(bad));
}
