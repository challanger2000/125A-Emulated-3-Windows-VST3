#pragma once
// Backend clock/accumulator continuation; calibration remains separate.
#include "core_state_envelope.h"
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>
namespace eiii::vst3 {
struct CoreAudioTiming {
 struct State {
  std::array<double,5> values{}; // rate, sample_rate, next, accum left/right
  uint64_t now=0,sample_words=0,output_frames=0;
  uint32_t oversample=0,phase=0;
  bool model_v2=false;
 };
 static constexpr uint32_t tag=0x32545541u;
 static void put64(std::vector<uint8_t>& p,uint64_t x){
  CoreStateEnvelope::put32(p,uint32_t(x));CoreStateEnvelope::put32(p,uint32_t(x>>32));
 }
 static uint64_t get64(uint8_t const* p){
  return uint64_t(CoreStateEnvelope::get32(p))|(uint64_t(CoreStateEnvelope::get32(p+4))<<32);
 }
 static bool valid(State const& s) {
  for(double x:s.values)if(!std::isfinite(x))return false;
  return s.values[0]>0 && s.values[1]>0 && s.values[2]>=0 &&
    s.sample_words>0 && s.oversample>0 && s.oversample<=64 &&
    s.phase<s.oversample;
 }
 static bool encode(State const& s,std::vector<uint8_t>& out) {
  if(!valid(s))return false;
  std::vector<uint8_t> p,w;CoreStateEnvelope::put32(p,tag);
  for(double x:s.values){uint64_t bits=0;std::memcpy(&bits,&x,8);put64(p,bits);}
  put64(p,s.now);put64(p,s.sample_words);
  CoreStateEnvelope::put32(p,s.oversample);CoreStateEnvelope::put32(p,s.phase);
  p.push_back(uint8_t(s.model_v2));put64(p,s.output_frames);
  if(!CoreStateEnvelope::encode(p,w))return false;
  out.swap(w);return true;
 }
 static bool decode(uint8_t const* data,size_t size,State& out) {
  std::vector<uint8_t> p;
  if(!CoreStateEnvelope::decode(data,size,p)||p.size()!=77||
     CoreStateEnvelope::get32(p.data())!=tag||p[68]>1)return false;
  State s{};size_t i=4;
  for(double& x:s.values){uint64_t bits=get64(p.data()+i);i+=8;std::memcpy(&x,&bits,8);}
  s.now=get64(p.data()+i);i+=8;
  s.sample_words=get64(p.data()+i);i+=8;
  s.oversample=CoreStateEnvelope::get32(p.data()+i);i+=4;
  s.phase=CoreStateEnvelope::get32(p.data()+i);i+=4;
  s.model_v2=p[i++]!=0;s.output_frames=get64(p.data()+i);
  if(!valid(s))return false;
  out=s;return true;
 }
};
}
