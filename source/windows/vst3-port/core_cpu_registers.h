// B02 portable NS32016 register file (separate from cycle timebase).
#pragma once
#include "core_state_envelope.h"
#include <array>
#include <cstdint>
#include <vector>
namespace eiii::vst3 {
struct CoreCpuRegisters {
 struct State {
  std::array<uint32_t,9> prefix{}; // ssp,pc,sb,fp,sp1,sp0,intbase,cfg,reserved
  std::array<uint32_t,8> r{};
  uint16_t sps=0,psr=0,mod=0;
  uint8_t lines=0; // nmi,int,wait,sequential,ready: five low bits
 };
 static constexpr uint32_t tag=0x31555043u; // CPU1
 static bool encode(State const& s,std::vector<uint8_t>& out) {
  if(s.prefix[8]!=0||(s.lines&~uint8_t(31)))return false;
  std::vector<uint8_t> body,wire;
  CoreStateEnvelope::put32(body,tag);
  for(auto v:s.prefix)CoreStateEnvelope::put32(body,v);
  for(auto v:s.r)CoreStateEnvelope::put32(body,v);
  CoreStateEnvelope::put32(body,uint32_t(s.sps)|(uint32_t(s.psr)<<16));
  CoreStateEnvelope::put32(body,uint32_t(s.mod)|(uint32_t(s.lines)<<16));
  if(!CoreStateEnvelope::encode(body,wire))return false;
  out.swap(wire);return true;
 }
 static bool decode(uint8_t const* p,size_t n,State& out) {
  std::vector<uint8_t> body;
  if(!CoreStateEnvelope::decode(p,n,body)||body.size()!=80||
     CoreStateEnvelope::get32(body.data())!=tag)return false;
  State s;size_t at=4;
  for(auto &v:s.prefix){v=CoreStateEnvelope::get32(body.data()+at);at+=4;}
  for(auto &v:s.r){v=CoreStateEnvelope::get32(body.data()+at);at+=4;}
  uint32_t a=CoreStateEnvelope::get32(body.data()+at);at+=4;
  uint32_t b=CoreStateEnvelope::get32(body.data()+at);
  s.sps=uint16_t(a);s.psr=uint16_t(a>>16);
  s.mod=uint16_t(b);s.lines=uint8_t(b>>16);
  if(s.prefix[8]!=0||(b>>24)!=0||(s.lines&~uint8_t(31)))return false;
  out=s;return true;
 }
};
}
