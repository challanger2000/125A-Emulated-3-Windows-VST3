// B02 HDD media identity and transfer cursor subsection; excludes shared SCSI state.
#pragma once
#include "core_state_envelope.h"
#include <array>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
namespace eiii::vst3 {
struct CoreHddSection {
 struct State {
  std::array<uint8_t,512> sector{};
  int32_t lba=0,cur_lba=-1,remaining_blocks=0,bytes_per_sector=0;
  std::vector<uint8_t> inquiry;
  std::string model_name;
  bool media_present=false,seek_model=false;
  uint64_t media_length=0,media_fingerprint=0;
  int32_t last_cylinder=-1;
  uint8_t interleave=1;
  uint32_t rpm=3600,seek_track_us=0,seek_range_us=0;
  double seek_exponent=1;
 };
 static constexpr uint32_t tag=0x31444448u;
 static void u64(std::vector<uint8_t>& v,uint64_t n) {
  CoreStateEnvelope::put32(v,uint32_t(n));
  CoreStateEnvelope::put32(v,uint32_t(n>>32));
 }
 static uint64_t get64(uint8_t const* p) {
  return uint64_t(CoreStateEnvelope::get32(p))|
         uint64_t(CoreStateEnvelope::get32(p+4))<<32;
 }
 static bool encode(State const& s,std::vector<uint8_t>& out) {
  if(s.inquiry.size()>4096||s.model_name.size()>255||
     s.interleave==0||!std::isfinite(s.seek_exponent)||
     s.seek_exponent<=0)return false;
  std::vector<uint8_t> payload,wire;
  CoreStateEnvelope::put32(payload,tag);
  payload.insert(payload.end(),s.sector.begin(),s.sector.end());
  for(int32_t n:{s.lba,s.cur_lba,s.remaining_blocks,s.bytes_per_sector,s.last_cylinder})
   CoreStateEnvelope::put32(payload,uint32_t(n));
  CoreStateEnvelope::put32(payload,uint32_t(s.inquiry.size()));
  payload.insert(payload.end(),s.inquiry.begin(),s.inquiry.end());
  CoreStateEnvelope::put32(payload,uint32_t(s.model_name.size()));
  payload.insert(payload.end(),s.model_name.begin(),s.model_name.end());
  CoreStateEnvelope::put32(payload,s.media_present?1:0);
  CoreStateEnvelope::put32(payload,s.seek_model?1:0);
  u64(payload,s.media_length);u64(payload,s.media_fingerprint);
  CoreStateEnvelope::put32(payload,s.interleave);
  CoreStateEnvelope::put32(payload,s.rpm);
  CoreStateEnvelope::put32(payload,s.seek_track_us);
  CoreStateEnvelope::put32(payload,s.seek_range_us);
  uint64_t bits;std::memcpy(&bits,&s.seek_exponent,8);u64(payload,bits);
  if(!CoreStateEnvelope::encode(payload,wire))return false;
  out.swap(wire);return true;
 }
 static bool decode(uint8_t const* p,size_t n,State& out) {
  std::vector<uint8_t> d;
  if(!CoreStateEnvelope::decode(p,n,d)||d.size()<592||
     CoreStateEnvelope::get32(d.data())!=tag)return false;
  size_t i=4;State s;
  for(auto& v:s.sector)v=d[i++];
  auto get=[&](){uint32_t v=CoreStateEnvelope::get32(d.data()+i);i+=4;return v;};
  s.lba=int32_t(get());s.cur_lba=int32_t(get());s.remaining_blocks=int32_t(get());
  s.bytes_per_sector=int32_t(get());s.last_cylinder=int32_t(get());
  if(d.size()-i<4)return false;
  uint32_t ilen=get();if(ilen>4096||ilen>d.size()-i)return false;
  s.inquiry.assign(d.begin()+i,d.begin()+i+ilen);i+=ilen;
  if(d.size()-i<4)return false;
  uint32_t mlen=get();if(mlen>255||mlen>d.size()-i)return false;
  s.model_name.assign(reinterpret_cast<char const*>(d.data()+i),mlen);i+=mlen;
  if(d.size()-i!=48)return false;
  uint32_t present=get(),seek=get();
  if(present>1||seek>1)return false;
  s.media_present=present;s.seek_model=seek;
  s.media_length=get64(d.data()+i);i+=8;
  s.media_fingerprint=get64(d.data()+i);i+=8;
  uint32_t interleave=get();if(interleave==0||interleave>255)return false;
  s.interleave=uint8_t(interleave);
  s.rpm=get();s.seek_track_us=get();s.seek_range_us=get();
  uint64_t bits=get64(d.data()+i);i+=8;
  std::memcpy(&s.seek_exponent,&bits,8);
  if(!std::isfinite(s.seek_exponent)||s.seek_exponent<=0)return false;
  out=std::move(s);return true;
 }
};
}
