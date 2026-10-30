#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include "../vst3-port/core_fir_section.h"
int main(){
 using W=eiii::vst3::CoreFirSection;
 eiii::fir_decimator a,b;a.design(192000,4);b.design(192000,4);
 double l=0,r=0;
 for(unsigned i=0;i<109;++i)a.push(std::sin(i*.1),std::cos(i*.09),l,r);
 W::State snapshot{},loaded{};
 assert(a.capture_recall_state(snapshot));
 std::vector<uint8_t> blob;assert(W::encode(snapshot,blob));
 assert(W::decode(blob.data(),blob.size(),loaded));
 assert(b.restore_recall_state(loaded));
 for(unsigned i=109;i<640;++i){
  double al=0,ar=0,bl=0,br=0;
  bool x=a.push(std::sin(i*.1),std::cos(i*.09),al,ar);
  bool y=b.push(std::sin(i*.1),std::cos(i*.09),bl,br);
  assert(x==y);if(x){assert(al==bl);assert(ar==br);}
 }
 auto corrupt=blob;corrupt.back()^=1;
 assert(!W::decode(corrupt.data(),corrupt.size(),loaded));
 assert(loaded.coefficients.size()==snapshot.coefficients.size());
}
