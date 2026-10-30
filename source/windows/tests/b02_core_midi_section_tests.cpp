#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <vector>
#include "../vst3-port/core_midi_section.h"
int main(){
 using C=eiii::vst3::CoreMidiSection;
 C::State s;s.events={{0.0,{0x90,60,100}},{0.25,{0x80,60,0}}};
 s.next=1;s.queue={0x90,60,100};s.head=1;s.bit=5;s.current=0x90;
 std::vector<uint8_t> bytes;assert(C::encode(s,bytes));
 C::State restored;assert(C::decode(bytes.data(),bytes.size(),restored));
 assert(restored.events.size()==2&&restored.events[1].time==0.25);
 assert(restored.queue==s.queue&&restored.head==1&&restored.bit==5);
 auto earlier=restored;
 bytes.back()^=1;
 // The subsection is not checksummed itself: the enclosing core envelope
 // must detect tampering. A truncated stream must still reject atomically.
 assert(!C::decode(bytes.data(),bytes.size()-1,restored));
 assert(restored.queue==earlier.queue&&restored.events[1].bytes==earlier.events[1].bytes);
 bytes[4]=255;bytes[5]=255;bytes[6]=255;bytes[7]=255;
 assert(!C::decode(bytes.data(),bytes.size(),restored));
 assert(restored.events.size()==2);
 std::vector<uint8_t> rejected{1,2,3};
 C::State invalid=s;invalid.head=10;
 assert(!C::encode(invalid,rejected));assert((rejected==std::vector<uint8_t>{1,2,3}));
}
