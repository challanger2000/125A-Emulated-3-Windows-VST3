#pragma once
#include "core_state_envelope.h"
#include <cstdint>
#include <vector>
namespace eiii::vst3 {
struct CoreCpuClock {
 struct State {uint64_t completed_cycles=0;int64_t debugger_pc=0;};
 static constexpr uint32_t tag=0x314b4c43u;
 static bool encode(State const& s,std::vector<uint8_t>& out){
  std::vector<uint8_t> payload,wire;
  CoreStateEnvelope::put32(payload,tag);
  for(auto value:{s.completed_cycles,uint64_t(s.debugger_pc)}){
   CoreStateEnvelope::put32(payload,uint32_t(value));
   CoreStateEnvelope::put32(payload,uint32_t(value>>32));
  }
  if(!CoreStateEnvelope::encode(payload,wire))return false;
  out.swap(wire);return true;
 }
 static bool decode(uint8_t const* p,size_t n,State& out){
  std::vector<uint8_t> payload;
  if(!CoreStateEnvelope::decode(p,n,payload)||payload.size()!=20||
     CoreStateEnvelope::get32(payload.data())!=tag)return false;
  auto read=[&](size_t i)->uint64_t{
   return uint64_t(CoreStateEnvelope::get32(payload.data()+i))|
         (uint64_t(CoreStateEnvelope::get32(payload.data()+i+4))<<32);
  };
  State s;s.completed_cycles=read(4);s.debugger_pc=int64_t(read(12));
  out=s;return true;
 }
};
}
