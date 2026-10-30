#pragma once
#include "core_state_envelope.h"
#include <array>
#include <cstdint>
#include <vector>
namespace eiii::vst3 {
struct CoreMotherboardRegisters {
 struct State {
  std::array<uint16_t,128> dig{};
  uint32_t rf_latch=0;
  std::array<uint16_t,80> cv{};
  std::array<uint32_t,16> start{},loop{},size{};
  std::array<bool,16> pending{};
  uint64_t n_rf=0,n_ctrl=0,n_cv=0;
  uint8_t irq_latch=0;
  bool irq_state=false;
 };
 static constexpr uint32_t tag=0x3152474du;
 static void put64(std::vector<uint8_t>& p,uint64_t n){
  CoreStateEnvelope::put32(p,uint32_t(n));
  CoreStateEnvelope::put32(p,uint32_t(n>>32));
 }
 static uint64_t get64(uint8_t const* p){
  return uint64_t(CoreStateEnvelope::get32(p))|
     uint64_t(CoreStateEnvelope::get32(p+4))<<32;
 }
 static bool encode(State const& s,std::vector<uint8_t>& out){
  std::vector<uint8_t> p,w;CoreStateEnvelope::put32(p,tag);
  for(auto x:s.dig)CoreStateEnvelope::put32(p,x);
  CoreStateEnvelope::put32(p,s.rf_latch);
  for(auto x:s.cv)CoreStateEnvelope::put32(p,x);
  for(auto const* a:{&s.start,&s.loop,&s.size})
   for(auto x:*a)CoreStateEnvelope::put32(p,x);
  for(auto x:s.pending)p.push_back(uint8_t(x));
  put64(p,s.n_rf);put64(p,s.n_ctrl);put64(p,s.n_cv);
  p.push_back(s.irq_latch);p.push_back(uint8_t(s.irq_state));
  if(!CoreStateEnvelope::encode(p,w))return false;
  out.swap(w);return true;
 }
 static bool decode(uint8_t const* data,size_t n,State& out){
  std::vector<uint8_t> p;
  if(!CoreStateEnvelope::decode(data,n,p)||p.size()!=1074||
     CoreStateEnvelope::get32(p.data())!=tag)return false;
  State s;size_t i=4;
  auto next=[&](){auto x=CoreStateEnvelope::get32(p.data()+i);i+=4;return x;};
  for(auto& x:s.dig){auto v=next();if(v>65535)return false;x=uint16_t(v);}
  s.rf_latch=next();
  for(auto& x:s.cv){auto v=next();if(v>65535)return false;x=uint16_t(v);}
  for(auto* a:{&s.start,&s.loop,&s.size})for(auto& x:*a)x=next();
  for(auto& x:s.pending){if(p[i]>1)return false;x=p[i++]!=0;}
  s.n_rf=get64(p.data()+i);i+=8;
  s.n_ctrl=get64(p.data()+i);i+=8;
  s.n_cv=get64(p.data()+i);i+=8;
  s.irq_latch=p[i++];if(p[i]>1)return false;s.irq_state=p[i++]!=0;
  out=s;return true;
 }
};
}
