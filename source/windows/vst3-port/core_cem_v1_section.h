#pragma once
#include "core_state_envelope.h"
#include "../../upstream/cem/eiii_cem3387.h"
#include <array>
#include <cmath>
#include <cstring>
#include <vector>
namespace eiii::vst3 {
struct CoreCemV1Section {
 using State=std::array<cem3387_voice::recall_state,16>;
 static constexpr uint32_t tag=0x31564d43u;
 static void put64(std::vector<uint8_t>& p,uint64_t x){
  CoreStateEnvelope::put32(p,uint32_t(x));CoreStateEnvelope::put32(p,uint32_t(x>>32));
 }
 static uint64_t get64(uint8_t const* p){
  return uint64_t(CoreStateEnvelope::get32(p))|(uint64_t(CoreStateEnvelope::get32(p+4))<<32);
 }
 static bool encode(State const& state,std::vector<uint8_t>& out){
  std::vector<uint8_t> data,wire;CoreStateEnvelope::put32(data,tag);
  for(auto const& v:state){
   if(!std::isfinite(v.values[0])||v.values[0]<=0)return false;
   for(double d:v.values){if(!std::isfinite(d))return false;uint64_t bits;std::memcpy(&bits,&d,8);put64(data,bits);}
   for(uint64_t x:v.keys)put64(data,x);
  }
  if(!CoreStateEnvelope::encode(data,wire))return false;
  out.swap(wire);return true;
 }
 static bool decode(uint8_t const* p,size_t n,State& out){
  std::vector<uint8_t> data;
  if(!CoreStateEnvelope::decode(p,n,data)||data.size()!=4+16*(38+5)*8||
     CoreStateEnvelope::get32(data.data())!=tag)return false;
  State candidate{};size_t off=4;
  for(auto& v:candidate){
   for(double& d:v.values){auto x=get64(data.data()+off);off+=8;std::memcpy(&d,&x,8);if(!std::isfinite(d))return false;}
   if(v.values[0]<=0)return false;
   for(uint64_t& x:v.keys){x=get64(data.data()+off);off+=8;}
  }
  out=std::move(candidate);return true;
 }
};
}
