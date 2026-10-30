#pragma once
#include "core_state_envelope.h"
#include <array>
#include <vector>
#include <cstdint>
namespace eiii::vst3 {
struct CoreSccRegisters {
 struct Channel {std::array<uint8_t,16> rr{},wr{};uint8_t wr7p=0;};
 struct State {Channel a,b;};
 static constexpr uint32_t tag=0x31434353u;
 static bool encode(State const& s,std::vector<uint8_t>& out) {
  std::vector<uint8_t> payload,wire;
  CoreStateEnvelope::put32(payload,tag);
  for(auto const* c:{&s.a,&s.b}){
   payload.insert(payload.end(),c->rr.begin(),c->rr.end());
   payload.insert(payload.end(),c->wr.begin(),c->wr.end());
   payload.push_back(c->wr7p);
  }
  if(!CoreStateEnvelope::encode(payload,wire))return false;
  out.swap(wire);return true;
 }
 static bool decode(uint8_t const* p,size_t n,State& out) {
  std::vector<uint8_t> payload;
  if(!CoreStateEnvelope::decode(p,n,payload)||payload.size()!=70||
     CoreStateEnvelope::get32(payload.data())!=tag)return false;
  State s;size_t i=4;
  for(auto* c:{&s.a,&s.b}){
   for(auto& x:c->rr)x=payload[i++];
   for(auto& x:c->wr)x=payload[i++];
   c->wr7p=payload[i++];
  }
  out=s;return true;
 }
};
}
