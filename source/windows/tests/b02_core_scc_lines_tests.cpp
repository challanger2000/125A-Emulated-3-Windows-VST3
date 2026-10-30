#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include "../vst3-port/core_scc_lines.h"
int main() {
 using T=eiii::vst3::CoreSccLines;
 T::State a,b;
 a.a.brg_counter=123;
 a.b.lines[12]=-4;
 std::vector<uint8_t> v;
 assert(T::encode(a,v));
 assert(T::decode(v.data(),v.size(),b));
 assert(b.a.brg_counter==123);
 assert(b.b.lines[12]==-4);
 v.back()^=1;
 assert(!T::decode(v.data(),v.size(),b));
 assert(b.a.brg_counter==123);
}
