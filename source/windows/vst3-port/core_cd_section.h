#pragma once
#include "core_state_envelope.h"
#include <array>
#include <cstdint>
#include <vector>
namespace eiii::vst3 {
struct CoreCdSection {
 struct State {
  std::array<uint8_t,2048> sector{};
  std::array<uint8_t,256> mode{};
  std::vector<uint8_t> transfer;
  uint64_t media_length=0,media_fingerprint=0;
  bool media_present=false,removal_prevented=false;
  int32_t media_sequence=0,lba=0,cached_sector=-1;
  uint32_t block_size=2048,transfer_position=0,cached_media_sequence=0;
  uint8_t mode_size=12,sotc=0;
 };
 static constexpr uint32_t tag=0x32444343u;
 static void put64(std::vector<uint8_t>& v,uint64_t n) {
  CoreStateEnvelope::put32(v,uint32_t(n));
  CoreStateEnvelope::put32(v,uint32_t(n>>32));
 }
 static uint64_t get64(uint8_t const* p) {
  return uint64_t(CoreStateEnvelope::get32(p))|
      (uint64_t(CoreStateEnvelope::get32(p+4))<<32);
 }
 static bool encode(State const& s,std::vector<uint8_t>& out) {
  if(s.transfer.size()>16u*1024u*1024u||s.transfer_position>s.transfer.size()||
     s.block_size==0)return false;
  std::vector<uint8_t> v,w;CoreStateEnvelope::put32(v,tag);
  v.insert(v.end(),s.sector.begin(),s.sector.end());
  v.insert(v.end(),s.mode.begin(),s.mode.end());
  CoreStateEnvelope::put32(v,uint32_t(s.transfer.size()));
  v.insert(v.end(),s.transfer.begin(),s.transfer.end());
  put64(v,s.media_length);put64(v,s.media_fingerprint);
  for(uint32_t x:{uint32_t(s.media_present),uint32_t(s.removal_prevented),
      uint32_t(s.media_sequence),uint32_t(s.lba),uint32_t(s.cached_sector),
      s.block_size,s.transfer_position,uint32_t(s.mode_size),uint32_t(s.sotc),s.cached_media_sequence})
   CoreStateEnvelope::put32(v,x);
  if(!CoreStateEnvelope::encode(v,w))return false;
  out.swap(w);return true;
 }
 static bool decode(uint8_t const* p,size_t n,State& out) {
  std::vector<uint8_t> v;
  if(!CoreStateEnvelope::decode(p,n,v)||v.size()<2368||
     CoreStateEnvelope::get32(v.data())!=tag)return false;
  State s;size_t i=4;
  for(auto& x:s.sector)x=v[i++];
  for(auto& x:s.mode)x=v[i++];
  uint32_t length=CoreStateEnvelope::get32(v.data()+i);i+=4;
  if(length>16u*1024u*1024u||length>v.size()-i||
     v.size()-i-length!=56)return false;
  s.transfer.assign(v.begin()+i,v.begin()+i+length);i+=length;
  s.media_length=get64(v.data()+i);i+=8;
  s.media_fingerprint=get64(v.data()+i);i+=8;
  auto next=[&](){auto x=CoreStateEnvelope::get32(v.data()+i);i+=4;return x;};
  auto present=next(),prevented=next();
  if(present>1||prevented>1)return false;
  s.media_present=present;s.removal_prevented=prevented;
  s.media_sequence=int32_t(next());s.lba=int32_t(next());
  s.cached_sector=int32_t(next());s.block_size=next();
  s.transfer_position=next();auto modeSize=next(),sotc=next();
  if(modeSize>255||sotc>255||s.block_size==0||
     s.transfer_position>s.transfer.size())return false;
  s.mode_size=uint8_t(modeSize);s.sotc=uint8_t(sotc);s.cached_media_sequence=next();
  out=std::move(s);return true;
 }
};
}
