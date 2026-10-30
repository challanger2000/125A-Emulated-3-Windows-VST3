#include "../../upstream/runtime/eiii_lockfree.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdint>
#include <cstdio>

int main() {
 eiii::spsc_queue<uint32_t> midi(8), panel(8);
 eiii::audio_ring output(16), input(16);
 for (int cycle=0;cycle<200;++cycle) {
  for(uint32_t k=1;k<=5;++k) {assert(midi.push(k));assert(panel.push(k+10));}
  const float samples[]={1,2,3,4,5,6,7,8};
  assert(output.write(samples,4)==4);
  assert(input.write(samples,4)==4);
  // Production engine::shutdown() applies these drains after joining its worker.
  uint32_t event=0;
  while(midi.pop(event)) {}
  while(panel.pop(event)) {}
  output.drain();input.drain();
  assert(midi.size()==0 && panel.size()==0);
  assert(output.fill()==0 && input.fill()==0);
  // Reactivated worker sees only newly produced events and frames.
  assert(midi.push(uint32_t(cycle+100)));
  assert(midi.pop(event) && event==uint32_t(cycle+100));
  assert(!midi.pop(event));
  const float fresh[]={9,10};
  assert(output.write(fresh,1)==1);
  float l=0,r=0;
  assert(output.read(&l,&r,1)==1 && l==9 && r==10);
  assert(output.fill()==0);
 }
 std::puts("PASS: 200 queue drain/restart cycles");
}
