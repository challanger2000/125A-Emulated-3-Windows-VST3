// B02 portable NS32081 FPU architectural and in-flight operation state.
#pragma once
#include "core_state_envelope.h"
#include <array>
#include <cstdint>
#include <vector>
namespace eiii::vst3 {
struct CoreFpuSection {
 struct Operand { uint32_t expected=0,issued=0;uint64_t value=0; };
 struct State {
  uint32_t fsr=0,state=0,tcy=0;
  uint8_t idbyte=0;
  uint16_t opword=0,status=0;
  std::array<Operand,3> op{};
  std::array<uint32_t,8> f{};
 };
 static constexpr uint32_t tag=0x31555046u; // FPU1
 static bool valid(State const& s) {
  if(s.state!=0&&s.state!=1&&s.state!=2&&s.state!=4&&s.state!=5)return false;
  for(auto const& o:s.op)if(o.expected>8||o.issued>o.expected)return false;
  return true;
 }
 static bool encode(State const& s,std::vector<uint8_t>& out) {
  if(!valid(s))return false;
  for(auto const& o:s.op)if(o.expected>8||o.issued>o.expected)return false;
  std::vector<uint8_t> body,wire;
  CoreStateEnvelope::put32(body,tag);
  CoreStateEnvelope::put32(body,s.fsr);CoreStateEnvelope::put32(body,s.state);
  CoreStateEnvelope::put32(body,s.tcy);CoreStateEnvelope::put32(body,s.idbyte);
  CoreStateEnvelope::put32(body,uint32_t(s.opword)|(uint32_t(s.status)<<16));
  for(auto const& o:s.op) {
   CoreStateEnvelope::put32(body,o.expected);CoreStateEnvelope::put32(body,o.issued);
   CoreStateEnvelope::put32(body,uint32_t(o.value));
   CoreStateEnvelope::put32(body,uint32_t(o.value>>32));
  }
  for(auto v:s.f)CoreStateEnvelope::put32(body,v);
  if(!CoreStateEnvelope::encode(body,wire))return false;
  out.swap(wire);return true;
 }
 static bool decode(uint8_t const* p,size_t n,State& out) {
  std::vector<uint8_t> body;
  if(!CoreStateEnvelope::decode(p,n,body)||body.size()!=104||
     CoreStateEnvelope::get32(body.data())!=tag)return false;
  size_t i=4;auto next=[&](){auto v=CoreStateEnvelope::get32(body.data()+i);i+=4;return v;};
  State s;s.fsr=next();s.state=next();s.tcy=next();
  uint32_t id=next();if(id>255)return false;s.idbyte=uint8_t(id);
  uint32_t flags=next();s.opword=uint16_t(flags);s.status=uint16_t(flags>>16);
  for(auto& o:s.op){
   o.expected=next();o.issued=next();uint32_t lo=next(),hi=next();
   o.value=uint64_t(lo)|(uint64_t(hi)<<32);
   if(o.expected>8||o.issued>o.expected)return false;
  }
  for(auto& v:s.f)v=next();
  if(!valid(s))return false;
  out=s;return true;
 }
};
}
