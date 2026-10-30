#pragma once
// Portable B02 F-Chip voice state. Does not capture analog CEM or decimator.
#include "core_state_envelope.h"
#include "../../upstream/fchip/eiii_fchip.h"
#include <array>
#include <cstdint>
#include <vector>
namespace eiii::vst3 {
struct CoreFchipSection {
 using State=std::array<fchip_bank::voice_state,16>;
 static constexpr uint32_t tag=0x31504346u;
 static bool valid(fchip_bank::voice_state const& v){
  return v.rate<=4095 && v.ctrl_phase<=1 && v.ctrl_hi<=255 &&
         (v.rf[0]&~0x1ffffffu)==0 && (v.rf[1]&~0x1ffffffu)==0 &&
         (v.rf[2]&~0x1ffffffu)==0 && (v.rf[3]&~0x1ffffffu)==0;
 }
 static bool encode(State const& s,std::vector<uint8_t>& out){
  std::vector<uint8_t> p,w;CoreStateEnvelope::put32(p,tag);
  for(auto const& v:s){
   if(!valid(v))return false;
   for(auto n:v.rf)CoreStateEnvelope::put32(p,n);
   CoreStateEnvelope::put32(p,v.phase);
   CoreStateEnvelope::put32(p,v.rate);
   p.push_back(uint8_t(v.active));p.push_back(v.ctrl_phase);p.push_back(v.ctrl_hi);
  }
  if(!CoreStateEnvelope::encode(p,w))return false;
  out.swap(w);return true;
 }
 static bool decode(uint8_t const* data,size_t size,State& out){
  std::vector<uint8_t> p;
  if(!CoreStateEnvelope::decode(data,size,p)||p.size()!=4+16*27||
     CoreStateEnvelope::get32(p.data())!=tag)return false;
  State tmp{};size_t off=4;
  for(auto& v:tmp){
   for(auto& n:v.rf){n=CoreStateEnvelope::get32(p.data()+off);off+=4;}
   v.phase=CoreStateEnvelope::get32(p.data()+off);off+=4;
   uint32_t rate=CoreStateEnvelope::get32(p.data()+off);off+=4;
   if(rate>4095||p[off]>1||p[off+1]>1)return false;
   v.rate=uint16_t(rate);v.active=p[off++]!=0;
   v.ctrl_phase=p[off++];v.ctrl_hi=p[off++];
   if(!valid(v))return false;
  }
  out=tmp;return true;
 }
};
}
