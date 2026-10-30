#pragma once
#include <cstdint>
#include <cmath>
namespace eiii::vst3 {
// VST3 control changes are delivered as normalized parameter points, not Events.
// Reserve 3 parameter IDs for each of the 16 MIDI channels.
struct MidiTimeOrder {
 template<class Point> bool operator()(const Point& a,const Point& b) const noexcept {
  return a.offset<b.offset || (a.offset==b.offset && a.order<b.order);
 }
};
struct MidiControllers {
 static constexpr uint32_t base=2000, per_channel=4;
 static constexpr uint32_t total=16*per_channel;
 // A14: VST3 normalized defaults are metadata exposed to the DAW.
 // Only pitchbend (slot 2) must initialize to the 14-bit neutral center
 // (8192). The 0.5 default decodes exactly to 8192 via round(0.5*16383).
 // Other controller defaults remain unchanged to preserve prior behavior.
 static constexpr double default_normalized(unsigned slot) noexcept {
  return slot==2 ? 0.5 : 0.0;
 }
 static constexpr uint32_t id(unsigned ch,unsigned slot) noexcept {return base+ch*per_channel+slot;}
 static bool decode(uint32_t parameter,double value,uint8_t (&msg)[3]) noexcept {
  if(parameter<base || parameter>=base+total || !std::isfinite(value)) return false;
  const unsigned index=parameter-base,ch=index/per_channel,slot=index%per_channel;
  if(value<0) value=0; else if(value>1) value=1;
  if(slot==0) { // modulation wheel, MIDI CC1
   msg[0]=uint8_t(0xb0|ch);msg[1]=1;msg[2]=uint8_t(value*127+0.5);
  } else if(slot==1) { // sustain pedal, MIDI CC64
   msg[0]=uint8_t(0xb0|ch);msg[1]=64;msg[2]=value>=0.5?127:0;
  } else if(slot==2) { // 14-bit pitch bend, low then high byte
   const unsigned bend=unsigned(value*16383+0.5);
   msg[0]=uint8_t(0xe0|ch);msg[1]=uint8_t(bend&127);msg[2]=uint8_t((bend>>7)&127);
  }
  if(slot==3) { // channel pressure (aftertouch)
   msg[0]=uint8_t(0xd0|ch);msg[1]=uint8_t(value*127+0.5);msg[2]=0;
  }
  return true;
 }
};
}
