#pragma once
#include "core_state_envelope.h"
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>
namespace eiii::vst3 {
struct CoreMotherboardPit {
 struct Channel {
  double clock_hz=0,last_t=0;
  int32_t mode=0,rw=0,bcd=0;
  uint32_t reload=0,count=0,load_delay=0,latch=0;
  std::array<bool,7> flags{};
  uint64_t rem=0;
 };
 using State=std::array<Channel,3>;
 static constexpr uint32_t tag=0x31544950u;
 static bool valid(Channel const& s) {
  return std::isfinite(s.clock_hz)&&s.clock_hz>=0 &&
    std::isfinite(s.last_t)&&s.last_t>=0 &&
    s.mode>=0&&s.mode<=5&&s.rw>=0&&s.rw<=3&&s.bcd>=0&&s.bcd<=1 &&
    s.reload<=65536&&s.count<=65536&&s.latch<=65536&&s.rem<10000000ULL;
 }
 static void put64(std::vector<uint8_t>& p,uint64_t v) {
  CoreStateEnvelope::put32(p,uint32_t(v));
  CoreStateEnvelope::put32(p,uint32_t(v>>32));
 }
 static uint64_t get64(uint8_t const* p) {
  return uint64_t(CoreStateEnvelope::get32(p)) |
    (uint64_t(CoreStateEnvelope::get32(p+4))<<32);
 }
 static bool encode(State const& s,std::vector<uint8_t>& out) {
  std::vector<uint8_t> p,w;CoreStateEnvelope::put32(p,tag);
  for(auto const& c:s) {
   if(!valid(c))return false;
   uint64_t bits=0;std::memcpy(&bits,&c.clock_hz,8);put64(p,bits);
   std::memcpy(&bits,&c.last_t,8);put64(p,bits);
   for(int32_t v:{c.mode,c.rw,c.bcd})CoreStateEnvelope::put32(p,uint32_t(v));
   for(uint32_t v:{c.reload,c.count,c.load_delay,c.latch})CoreStateEnvelope::put32(p,v);
   for(bool flag:c.flags)p.push_back(uint8_t(flag));
   put64(p,c.rem);
  }
  if(!CoreStateEnvelope::encode(p,w))return false;
  out.swap(w);return true;
 }
 static bool decode(uint8_t const* data,size_t size,State& out) {
  std::vector<uint8_t> p;
  if(!CoreStateEnvelope::decode(data,size,p)||p.size()!=181||
      CoreStateEnvelope::get32(p.data())!=tag)return false;
  State s{};size_t i=4;
  auto next=[&](){uint32_t v=CoreStateEnvelope::get32(p.data()+i);i+=4;return v;};
  for(auto& c:s) {
   uint64_t bits=get64(p.data()+i);i+=8;std::memcpy(&c.clock_hz,&bits,8);
   bits=get64(p.data()+i);i+=8;std::memcpy(&c.last_t,&bits,8);
   c.mode=int32_t(next());c.rw=int32_t(next());c.bcd=int32_t(next());
   c.reload=next();c.count=next();c.load_delay=next();c.latch=next();
   for(auto& f:c.flags){if(p[i]>1)return false;f=p[i++]!=0;}
   c.rem=get64(p.data()+i);i+=8;
   if(!valid(c))return false;
  }
  out=s;return true;
 }
};
}
