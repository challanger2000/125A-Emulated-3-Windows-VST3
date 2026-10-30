// Snapshot compatibility and small state omitted by the original subsections.
// Hashes identify external ROM/overlay bytes; no ROM payload is serialized.
#pragma once
#include "core_state_envelope.h"
#include <array>
namespace eiii::vst3 {
struct CoreMachineContext {
 struct State {
  uint64_t rom=0;
  std::array<uint64_t,8> overlays{};
  bool pit1_last=true;
  std::array<uint16_t,2> sync_pattern{};
  std::array<uint8_t,2> rr7p{};
 };
 static constexpr uint32_t tag=0x31585443u;
 static bool encode(State const& s,std::vector<uint8_t>& out){
  std::vector<uint8_t> b;CoreStateEnvelope::put32(b,tag);
  auto put=[&](uint64_t v){CoreStateEnvelope::put32(b,uint32_t(v));CoreStateEnvelope::put32(b,uint32_t(v>>32));};
  put(s.rom);for(auto v:s.overlays)put(v);
  CoreStateEnvelope::put32(b,s.pit1_last?1:0);
  for(auto v:s.sync_pattern)CoreStateEnvelope::put32(b,v);
  for(auto v:s.rr7p)CoreStateEnvelope::put32(b,v);
  return CoreStateEnvelope::encode(b,out);
 }
 static bool decode(uint8_t const* p,size_t n,State& out){
  std::vector<uint8_t> b;
  if(!CoreStateEnvelope::decode(p,n,b)||b.size()!=96||CoreStateEnvelope::get32(b.data())!=tag)return false;
  size_t i=4;auto get=[&](){uint32_t v=CoreStateEnvelope::get32(b.data()+i);i+=4;return v;};
  auto get64=[&](){uint64_t lo=get();return lo|uint64_t(get())<<32;};
  State s;s.rom=get64();for(auto& v:s.overlays)v=get64();
  auto bit=get();if(bit>1)return false;s.pit1_last=bit!=0;
  for(auto& v:s.sync_pattern){auto x=get();if(x>65535)return false;v=uint16_t(x);}
  for(auto& v:s.rr7p){auto x=get();if(x>255)return false;v=uint8_t(x);}
  out=s;return true;
 }
};
}
