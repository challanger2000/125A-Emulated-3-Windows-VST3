#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include "../../upstream/host/eiii_decimator.h"
int main(){
 eiii::fir_decimator original,restored;
 original.design(192000,4);
 restored.design(192000,4);
 double l=0,r=0;
 for(int i=0;i<107;++i)original.push(std::sin(i*0.2),std::cos(i*0.1),l,r);
 eiii::fir_decimator::recall_state state{};
 assert(original.capture_recall_state(state));
 assert(restored.restore_recall_state(state));
 for(int i=107;i<700;++i){
  double al=0,ar=0,bl=0,br=0;
  bool a=original.push(std::sin(i*0.2),std::cos(i*0.1),al,ar);
  bool b=restored.push(std::sin(i*0.2),std::cos(i*0.1),bl,br);
  assert(a==b);
  if(a){assert(al==bl);assert(ar==br);}
 }
 auto bad=state;bad.position=bad.coefficients.size();
 assert(!restored.restore_recall_state(bad));
 bad=state;bad.coefficients[0]=NAN;
 assert(!restored.restore_recall_state(bad));
}
