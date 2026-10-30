#pragma once
#include "core_state_envelope.h"
#include <array>
#include <cstdint>
#include <vector>
namespace eiii::vst3 {
struct CoreMotherboardLcd {
 struct State {
  std::array<uint8_t,128> ddram{};
  std::array<uint8_t,64> cgram{};
  int32_t ac=0,dir=1,shift=0,lines=2;
  bool cgram_active=false,shift_on=false,display_on=false,cursor_on=false,blink_on=false;
  uint64_t busy_until_bits=0;
 };
 static constexpr uint32_t tag=0x3144434cu;
 static bool valid(State const& s){
  return s.ac>=0&&s.ac<=127&&(s.dir==1||s.dir==-1)&&
     s.shift>=0&&s.shift<80&&(s.lines==1||s.lines==2);
 }
 static bool encode(State const& s,std::vector<uint8_t>& out){
  if(!valid(s))return false;
  std::vector<uint8_t> v,w;
  CoreStateEnvelope::put32(v,tag);
  v.insert(v.end(),s.ddram.begin(),s.ddram.end());
  v.insert(v.end(),s.cgram.begin(),s.cgram.end());
  for(int32_t x:{s.ac,s.dir,s.shift,s.lines})CoreStateEnvelope::put32(v,uint32_t(x));
  for(bool x:{s.cgram_active,s.shift_on,s.display_on,s.cursor_on,s.blink_on})v.push_back(uint8_t(x));
  CoreStateEnvelope::put32(v,uint32_t(s.busy_until_bits));
  CoreStateEnvelope::put32(v,uint32_t(s.busy_until_bits>>32));
  if(!CoreStateEnvelope::encode(v,w))return false;
  out.swap(w);return true;
 }
 static bool decode(uint8_t const* p,size_t n,State& out){
  std::vector<uint8_t> v;
  if(!CoreStateEnvelope::decode(p,n,v)||v.size()!=225||
     CoreStateEnvelope::get32(v.data())!=tag)return false;
  State s;size_t i=4;
  for(auto& x:s.ddram)x=v[i++];
  for(auto& x:s.cgram)x=v[i++];
  auto next=[&](){auto x=CoreStateEnvelope::get32(v.data()+i);i+=4;return x;};
  s.ac=int32_t(next());s.dir=int32_t(next());s.shift=int32_t(next());s.lines=int32_t(next());
  bool* flags[]={&s.cgram_active,&s.shift_on,&s.display_on,&s.cursor_on,&s.blink_on};
  for(auto* x:flags){if(v[i]>1)return false;*x=v[i++]!=0;}
  auto lo=next(),hi=next();s.busy_until_bits=uint64_t(lo)|(uint64_t(hi)<<32);
  if(!valid(s))return false;
  out=s;return true;
 }
};
}
