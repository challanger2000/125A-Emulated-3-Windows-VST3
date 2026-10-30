// B02 NCR5380 controller register and line state, pointer-free.
#pragma once
#include "core_state_envelope.h"
#include <cstdint>
#include <vector>
namespace eiii::vst3 {
struct CoreNcrSection {
 struct State { uint32_t phase=0,ctrl=0;uint8_t regs[7]{};uint8_t flags=0; };
 static constexpr uint32_t tag=0x3152434eu; // NCR1
 static bool encode(State const& s,std::vector<uint8_t>& out) {
  if(s.flags>15)return false;
  std::vector<uint8_t> data,wire;
  CoreStateEnvelope::put32(data,tag);
  CoreStateEnvelope::put32(data,s.phase);
  CoreStateEnvelope::put32(data,s.ctrl);
  for(auto v:s.regs)CoreStateEnvelope::put32(data,v);
  CoreStateEnvelope::put32(data,s.flags);
  if(!CoreStateEnvelope::encode(data,wire))return false;
  out.swap(wire);return true;
 }
 static bool decode(uint8_t const* p,size_t n,State& out) {
  std::vector<uint8_t> body;
  if(!CoreStateEnvelope::decode(p,n,body)||body.size()!=44||
     CoreStateEnvelope::get32(body.data())!=tag)return false;
  State s;s.phase=CoreStateEnvelope::get32(body.data()+4);
  s.ctrl=CoreStateEnvelope::get32(body.data()+8);
  for(unsigned i=0;i<7;++i){
   uint32_t v=CoreStateEnvelope::get32(body.data()+12+4*i);
   if(v>255)return false;s.regs[i]=uint8_t(v);
  }
  uint32_t f=CoreStateEnvelope::get32(body.data()+40);
  if(f>15)return false;s.flags=uint8_t(f);
  out=s;return true;
 }
};
}
