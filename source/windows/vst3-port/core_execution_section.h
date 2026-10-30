// B02 CPU execution-position transport. Only a subsection of full CPU state.
#pragma once
#include "core_state_envelope.h"
#include <cstdint>
#include <vector>
namespace eiii::vst3 {
struct CoreExecutionSection {
 struct State { uint32_t pc=0; uint64_t completed_cycles=0; };
 static constexpr uint32_t tag=0x31455845u; // EXE1
 static bool encode(State const& s,std::vector<uint8_t>& out) {
  std::vector<uint8_t> body,wire;
  CoreStateEnvelope::put32(body,tag);
  CoreStateEnvelope::put32(body,s.pc);
  CoreStateEnvelope::put32(body,uint32_t(s.completed_cycles));
  CoreStateEnvelope::put32(body,uint32_t(s.completed_cycles>>32));
  if(!CoreStateEnvelope::encode(body,wire))return false;
  out.swap(wire);return true;
 }
 static bool decode(uint8_t const* data,size_t n,State& out) {
  std::vector<uint8_t> body;
  if(!CoreStateEnvelope::decode(data,n,body)||body.size()!=16||
     CoreStateEnvelope::get32(body.data())!=tag)return false;
  State tmp;
  tmp.pc=CoreStateEnvelope::get32(body.data()+4);
  tmp.completed_cycles=uint64_t(CoreStateEnvelope::get32(body.data()+8))|
      (uint64_t(CoreStateEnvelope::get32(body.data()+12))<<32);
  out=tmp;return true;
 }
};
}
