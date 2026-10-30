#pragma once
// Portable FIR continuation data: coefficients, doubled stereo rings, phase.
#include "core_state_envelope.h"
#include "../../upstream/host/eiii_decimator.h"
#include <cstdint>
#include <cstring>
#include <vector>
#include <limits>
namespace eiii::vst3 {
struct CoreFirSection {
 using State=fir_decimator::recall_state;
 static constexpr uint32_t tag=0x31524946u;
 static void put64(std::vector<uint8_t>& out,uint64_t n){
  CoreStateEnvelope::put32(out,uint32_t(n));
  CoreStateEnvelope::put32(out,uint32_t(n>>32));
 }
 static uint64_t read64(uint8_t const* p){
  return uint64_t(CoreStateEnvelope::get32(p))|
     (uint64_t(CoreStateEnvelope::get32(p+4))<<32);
 }
 static bool encode(State const& state,std::vector<uint8_t>& out){
  if(!fir_decimator::valid_recall_state(state))return false;
  std::vector<uint8_t> raw,wrapped;
  CoreStateEnvelope::put32(raw,tag);
  CoreStateEnvelope::put32(raw,uint32_t(state.coefficients.size()));
  CoreStateEnvelope::put32(raw,uint32_t(state.position));
  CoreStateEnvelope::put32(raw,state.ratio);
  CoreStateEnvelope::put32(raw,state.phase);
  auto emit=[&](double d){
   uint64_t u=0;static_assert(sizeof(d)==sizeof(u));
   std::memcpy(&u,&d,8);put64(raw,u);
  };
  for(double d:state.coefficients)emit(d);
  for(double d:state.left)emit(d);
  for(double d:state.right)emit(d);
  if(!CoreStateEnvelope::encode(raw,wrapped))return false;
  out.swap(wrapped);return true;
 }
 static bool decode(uint8_t const* bytes,size_t size,State& out){
  std::vector<uint8_t> raw;
  if(!CoreStateEnvelope::decode(bytes,size,raw)||raw.size()<20||
     CoreStateEnvelope::get32(raw.data())!=tag)return false;
  uint32_t taps=CoreStateEnvelope::get32(raw.data()+4);
  if(taps==0||taps>8192||raw.size()!=20u+uint64_t(taps)*40u)return false;
  State candidate{};
  candidate.position=CoreStateEnvelope::get32(raw.data()+8);
  candidate.ratio=CoreStateEnvelope::get32(raw.data()+12);
  candidate.phase=CoreStateEnvelope::get32(raw.data()+16);
  candidate.coefficients.resize(taps);
  candidate.left.resize(taps*2u);
  candidate.right.resize(taps*2u);
  size_t offset=20;
  auto load=[&](double& d){
   uint64_t u=read64(raw.data()+offset);offset+=8;std::memcpy(&d,&u,8);
  };
  for(double& d:candidate.coefficients)load(d);
  for(double& d:candidate.left)load(d);
  for(double& d:candidate.right)load(d);
  if(!fir_decimator::valid_recall_state(candidate))return false;
  out=std::move(candidate);return true;
 }
};
}
