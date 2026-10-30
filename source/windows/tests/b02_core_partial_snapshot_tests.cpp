#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <vector>
#include "../vst3-port/core_partial_snapshot.h"
int main(){
 using C=eiii::vst3::CorePartialSnapshot;
 using M=eiii::vst3::CoreMidiSection;
 std::vector<uint16_t> r(0x10000),s(0x400000);
 r[3]=0x1234;s[42]=0xabcd;s.back()=0x9876;
 M::State midi;midi.events={{0.0,{0x90,60,100}},{0.5,{0x80,60,0}}};
 midi.next=1;midi.queue={0x90,60};midi.head=1;midi.bit=3;
 std::vector<uint8_t> bytes;
 assert(C::encode(r.data(),r.size(),s.data(),s.size(),midi,bytes));
 std::vector<uint16_t> rr,ss;M::State restored;
 assert(C::decode(bytes.data(),bytes.size(),rr,ss,restored));
 assert(rr==r&&ss==s&&restored.events.size()==2&&restored.queue==midi.queue);
 bytes.back()^=1;
 assert(!C::decode(bytes.data(),bytes.size(),rr,ss,restored));
 assert(rr==r&&ss==s&&restored.events.size()==2);
 bytes.back()^=1;
 assert(!C::decode(bytes.data(),bytes.size()-1,rr,ss,restored));
 assert(rr==r&&ss==s);
 bytes[12]^=1;
 assert(!C::decode(bytes.data(),bytes.size(),rr,ss,restored));
 assert(rr==r&&ss==s);
}
