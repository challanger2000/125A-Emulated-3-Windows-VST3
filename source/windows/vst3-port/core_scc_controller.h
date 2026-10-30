#pragma once
#include "core_state_envelope.h"
#include <array>
#include <cstdint>
#include <vector>
namespace eiii::vst3 {
struct CoreSccController {
 struct State {
  uint8_t wr9=0,wr0_ptrbits=0;
  int32_t out_int_state=0;
  std::array<int32_t,6> int_state{},int_source{};
 };
 static constexpr uint32_t tag=0x31435353u;
 static bool valid(State const& s){
  if(s.wr0_ptrbits>15||s.out_int_state<0||s.out_int_state>1)return false;
  for(size_t i=0;i<6;++i)
   if(s.int_state[i]<0||s.int_state[i]>3||
      s.int_source[i]<0||s.int_source[i]>3)return false;
  return true;
 }
 static bool encode(State const& s,std::vector<uint8_t>& out){
  if(!valid(s))return false;
  std::vector<uint8_t> p,w;
  CoreStateEnvelope::put32(p,tag);
  CoreStateEnvelope::put32(p,s.wr9);
  CoreStateEnvelope::put32(p,s.wr0_ptrbits);
  CoreStateEnvelope::put32(p,uint32_t(s.out_int_state));
  for(auto x:s.int_state)CoreStateEnvelope::put32(p,uint32_t(x));
  for(auto x:s.int_source)CoreStateEnvelope::put32(p,uint32_t(x));
  if(!CoreStateEnvelope::encode(p,w))return false;
  out.swap(w);return true;
 }
 static bool decode(uint8_t const* data,size_t size,State& out){
  std::vector<uint8_t> p;
  if(!CoreStateEnvelope::decode(data,size,p)||p.size()!=64||
     CoreStateEnvelope::get32(p.data())!=tag)return false;
  size_t i=4;
  auto next=[&](){auto n=CoreStateEnvelope::get32(p.data()+i);i+=4;return n;};
  State s;
  auto wr9=next(),ptr=next();if(wr9>255||ptr>15)return false;
  s.wr9=uint8_t(wr9);s.wr0_ptrbits=uint8_t(ptr);
  s.out_int_state=int32_t(next());
  for(auto& x:s.int_state)x=int32_t(next());
  for(auto& x:s.int_source)x=int32_t(next());
  if(!valid(s))return false;
  out=s;return true;
 }
};
}
