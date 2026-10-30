#include "../vst3-port/note_off_recovery.h"
#include <array>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdio>
#include <vector>
int main() {
 eiii::vst3::NoteOffRecovery r;
 const uint8_t off[3]={0x8d,60,0};
 for(size_t i=0;i<r.capacity;++i) assert(r.defer(off));
 assert(r.pending()==r.capacity);
 assert(r.defer(off)); // overflow -> emergency stage on channel 14
 assert(r.panic_stage(13)==1);
 int calls=0;
 r.retry([&](const uint8_t*) {++calls;return false;});
 assert(calls==1 && r.pending()==r.capacity && r.panic_stage(13)==1);
 std::vector<std::array<uint8_t,3>> delivered;
 for(size_t block=0;block<r.capacity/r.max_retry_per_block+2;++block) {
  const size_t before=delivered.size();
  r.retry([&](const uint8_t* data) {
   delivered.push_back({data[0],data[1],data[2]});return true;
  });
  assert(delivered.size()-before<=r.max_retry_per_block);
 }
 assert(r.pending()==0 && r.panic_stage(13)==0);
 assert(delivered.size()==r.capacity+2);
 for(size_t i=0;i<r.capacity;++i)assert(delivered[i]==(std::array<uint8_t,3>{0x8d,60,0}));
 assert(delivered[r.capacity]==(std::array<uint8_t,3>{0xbd,64,0}));
 assert(delivered[r.capacity+1]==(std::array<uint8_t,3>{0xbd,120,0}));
 // Failure between sustain release and all-sound-off must preserve stage.
 r.defer(off);for(size_t i=1;i<r.capacity;++i)r.defer(off);
 r.defer(off);
 for(size_t block=0;block<r.capacity/r.max_retry_per_block+2;++block)
  r.retry([&](const uint8_t* data) {return data[0]!=0xbd || data[1]!=120;});
 assert(r.pending()==0 && r.panic_stage(13)==2);
 r.retry([&](const uint8_t*) {return true;});
 assert(r.panic_stage(13)==0);
 r.reset();
 const uint8_t a[3]={0x80,60,0}, b[3]={0x80,61,0};
 int immediate=0;
 assert(r.deliver(a,[&](const uint8_t*) { ++immediate; return false; }));
 assert(r.deliver(b,[&](const uint8_t*) { ++immediate; return true; }));
 assert(immediate==1 && r.pending()==2);
 std::vector<unsigned> ordered;
 r.retry([&](const uint8_t* m) { ordered.push_back(m[1]); return true; });
 assert(ordered.size()==2 && ordered[0]==60 && ordered[1]==61);
 assert(r.deliver(b,[&](const uint8_t*) { ++immediate; return true; }));
 assert(immediate==2 && r.pending()==0);
 r.reset();assert(r.pending()==0);
 std::puts("PASS: queue saturation, replay ordering, sustain and all-sound-off retry");
}
