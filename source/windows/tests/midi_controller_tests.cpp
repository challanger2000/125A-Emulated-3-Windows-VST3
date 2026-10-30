#include "../vst3-port/midi_controllers.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdio>
#include <algorithm>
#include <array>
int main() {
 using eiii::vst3::MidiControllers;
 uint8_t m[3]{};
 for(unsigned ch=0;ch<16;++ch) {
  assert(MidiControllers::decode(MidiControllers::id(ch,0),0,m));
  assert(m[0]==(0xb0|ch) && m[1]==1 && m[2]==0);
  assert(MidiControllers::decode(MidiControllers::id(ch,0),1,m) && m[2]==127);
  assert(MidiControllers::decode(MidiControllers::id(ch,1),0.49,m));
  assert(m[0]==(0xb0|ch) && m[1]==64 && m[2]==0);
  assert(MidiControllers::decode(MidiControllers::id(ch,1),0.5,m) && m[2]==127);
  assert(MidiControllers::decode(MidiControllers::id(ch,2),0,m));
  assert(m[0]==(0xe0|ch) && m[1]==0 && m[2]==0);
  assert(MidiControllers::decode(MidiControllers::id(ch,3),0.75,m));
  assert(m[0]==(0xd0|ch) && m[1]==95 && m[2]==0);
  assert(MidiControllers::decode(MidiControllers::id(ch,2),1,m));
  assert(m[1]==127 && m[2]==127);
 }
 assert(MidiControllers::decode(MidiControllers::id(0,2),0.5,m));
 assert(m[1]==0 && m[2]==64);
 assert(!MidiControllers::decode(MidiControllers::base-1,0.5,m));
 assert(!MidiControllers::decode(MidiControllers::base+MidiControllers::total,0.5,m));
 struct Point {int offset;unsigned order;int value;};
 std::array<Point,5> points{{{30,0,1},{2,1,2},{30,2,3},{0,3,4},{2,4,5}}};
 std::sort(points.begin(),points.end(),eiii::vst3::MidiTimeOrder{});
 assert(points[0].value==4 && points[1].value==2 &&
        points[2].value==5 && points[3].value==1 && points[4].value==3);
 std::puts("PASS: B08 16 channels, modulation, sustain, bend, channel pressure, bounds");
}
