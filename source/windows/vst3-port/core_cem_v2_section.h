#pragma once
// B02: V2 DSP continuation for an identically configured CEM bank.
// Configuration/calibration is NOT transported by this section.
#include "core_state_envelope.h"
#include "../../upstream/cem/eiii_cem3387_v2.h"
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>
namespace eiii::vst3 {
struct CoreCemV2Section {
 using State=cem3387_v2_bank::recall_state;
 static constexpr uint32_t tag=0x32564d43u;
 static void u64(std::vector<uint8_t>& p,uint64_t v){
  CoreStateEnvelope::put32(p,uint32_t(v));CoreStateEnvelope::put32(p,uint32_t(v>>32));
 }
 static uint64_t get64(uint8_t const* p){
  return uint64_t(CoreStateEnvelope::get32(p))|
   (uint64_t(CoreStateEnvelope::get32(p+4))<<32);
 }
 static void real(std::vector<uint8_t>& p,double v){
  uint64_t bits=0;std::memcpy(&bits,&v,8);u64(p,bits);
 }
 static bool encode(State const& s,std::vector<uint8_t>& out){
  std::vector<uint8_t> p,w;CoreStateEnvelope::put32(p,tag);
  for(auto const& v:s.voices){
   for(double x:v.values){if(!std::isfinite(x))return false;real(p,x);}
   for(uint64_t x:v.keys)u64(p,x);
   CoreStateEnvelope::put32(p,v.rng);
   for(double x:v.cv.scalars){if(!std::isfinite(x))return false;real(p,x);}
   for(double x:v.cv.coefficients){if(!std::isfinite(x))return false;real(p,x);}
   for(double x:v.cv.targets){if(!std::isfinite(x))return false;real(p,x);}
   for(double x:v.cv.pins){if(!std::isfinite(x))return false;real(p,x);}
   if(v.cv.diode.size()>100000)return false;
   CoreStateEnvelope::put32(p,uint32_t(v.cv.diode.size()));
   for(float x:v.cv.diode){if(!std::isfinite(x))return false;
    uint32_t bits=0;std::memcpy(&bits,&x,4);CoreStateEnvelope::put32(p,bits);}
  }
  for(double x:s.output.values){if(!std::isfinite(x))return false;real(p,x);}
  if(!CoreStateEnvelope::encode(p,w))return false;
  out.swap(w);return true;
 }
 static bool decode(uint8_t const* bytes,size_t n,State& out){
  std::vector<uint8_t> p;
  if(!CoreStateEnvelope::decode(bytes,n,p)||p.size()<8||
   CoreStateEnvelope::get32(p.data())!=tag)return false;
  State tmp{};size_t i=4;
  auto u32=[&](uint32_t& x)->bool{
   if(p.size()-i<4)return false;x=CoreStateEnvelope::get32(p.data()+i);i+=4;return true;};
  auto u64read=[&](uint64_t& x)->bool{
   if(p.size()-i<8)return false;x=get64(p.data()+i);i+=8;return true;};
  auto rd=[&](double& x)->bool{
   uint64_t bits=0;if(!u64read(bits))return false;
   std::memcpy(&x,&bits,8);return std::isfinite(x);};
  for(auto& v:tmp.voices){
   for(auto& x:v.values)if(!rd(x))return false;
   if(v.values[0]<=0)return false;
   for(auto& x:v.keys)if(!u64read(x))return false;
   if(!u32(v.rng))return false;
   for(auto& x:v.cv.scalars)if(!rd(x))return false;
   if(v.cv.scalars[0]<=0||v.cv.scalars[1]<=0||v.cv.scalars[2]<=0||
      v.cv.scalars[3]<=0||v.cv.scalars[4]<=0)return false;
   for(auto& x:v.cv.coefficients)if(!rd(x))return false;
   for(auto& x:v.cv.targets)if(!rd(x))return false;
   for(auto& x:v.cv.pins)if(!rd(x))return false;
   uint32_t count=0;if(!u32(count)||count>100000||
     (p.size()-i)/4<count)return false;
   v.cv.diode.resize(count);
   for(auto& x:v.cv.diode){uint32_t bits=0;if(!u32(bits))return false;
    std::memcpy(&x,&bits,4);if(!std::isfinite(x))return false;}
  }
  for(auto& x:tmp.output.values)if(!rd(x))return false;
  if(i!=p.size()||tmp.output.values[12]<=0)return false;
  out=std::move(tmp);return true;
 }
};
}
