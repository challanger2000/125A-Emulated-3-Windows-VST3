#pragma once
#include "core_state_envelope.h"
#include <array>
#include <cstdint>
#include <vector>
namespace eiii::vst3 {
struct CoreSccLines {
 struct Channel {
  uint16_t brg_counter=0;
  uint32_t brg_rate=0,delayed_tx_brg_change=0;
  uint8_t rx_error=0,tx_error=0,extint_latch=0,extint_states=0;
  std::array<int32_t,13> lines{};
 };
 struct State {Channel a,b;};
 static constexpr uint32_t tag=0x314c4353u;
 static bool encode(State const& s,std::vector<uint8_t>& out){
  std::vector<uint8_t> p,w;CoreStateEnvelope::put32(p,tag);
  for(auto const* c:{&s.a,&s.b}){
   CoreStateEnvelope::put32(p,c->brg_counter);
   CoreStateEnvelope::put32(p,c->brg_rate);
   CoreStateEnvelope::put32(p,c->delayed_tx_brg_change);
   p.push_back(c->rx_error);p.push_back(c->tx_error);
   p.push_back(c->extint_latch);p.push_back(c->extint_states);
   for(auto x:c->lines)CoreStateEnvelope::put32(p,uint32_t(x));
  }
  if(!CoreStateEnvelope::encode(p,w))return false;
  out.swap(w);return true;
 }
 static bool decode(uint8_t const* data,size_t size,State& out){
  std::vector<uint8_t> p;
  if(!CoreStateEnvelope::decode(data,size,p)||p.size()!=140||
     CoreStateEnvelope::get32(p.data())!=tag)return false;
  State s;size_t i=4;
  for(auto* c:{&s.a,&s.b}){
   auto next=[&](){auto x=CoreStateEnvelope::get32(p.data()+i);i+=4;return x;};
   auto count=next();if(count>65535)return false;
   c->brg_counter=uint16_t(count);
   c->brg_rate=next();c->delayed_tx_brg_change=next();
   c->rx_error=p[i++];c->tx_error=p[i++];
   c->extint_latch=p[i++];c->extint_states=p[i++];
   for(auto& x:c->lines)x=int32_t(next());
  }
  out=s;return true;
 }
};
}
