// B02 portable NSCSI bus lines and stable device identity roster.
#pragma once
#include "core_state_envelope.h"
#include <array>
#include <string>
#include <vector>
#include <cstdint>
namespace eiii::vst3 {
struct CoreScsiBusSection {
 struct Device {std::string tag;uint32_t ctrl=0,wait_ctrl=0,data=0;};
 struct State {uint32_t ctrl=0,data=0;std::vector<Device> devices;};
 static constexpr uint32_t tag=0x31425353u; // SSB1
 static bool encode(State const& s,std::vector<uint8_t>& out) {
  if(s.devices.size()>16)return false;
  std::vector<uint8_t> body,wire;
  CoreStateEnvelope::put32(body,tag);CoreStateEnvelope::put32(body,uint32_t(s.devices.size()));
  CoreStateEnvelope::put32(body,s.ctrl);CoreStateEnvelope::put32(body,s.data);
  for(auto const& d:s.devices){
   if(d.tag.size()>255)return false;
   CoreStateEnvelope::put32(body,uint32_t(d.tag.size()));
   body.insert(body.end(),d.tag.begin(),d.tag.end());
   CoreStateEnvelope::put32(body,d.ctrl);CoreStateEnvelope::put32(body,d.wait_ctrl);
   CoreStateEnvelope::put32(body,d.data);
  }
  if(!CoreStateEnvelope::encode(body,wire))return false;
  out.swap(wire);return true;
 }
 static bool decode(uint8_t const* p,size_t n,State& out) {
  std::vector<uint8_t> body;
  if(!CoreStateEnvelope::decode(p,n,body)||body.size()<16||
     CoreStateEnvelope::get32(body.data())!=tag)return false;
  size_t i=4;auto get=[&](){auto v=CoreStateEnvelope::get32(body.data()+i);i+=4;return v;};
  uint32_t count=get();if(count>16)return false;
  State s;s.ctrl=get();s.data=get();
  for(uint32_t k=0;k<count;++k){
   if(i>body.size()||body.size()-i<4)return false;
   uint32_t len=get();if(len>255||len>body.size()-i)return false;
   Device d;d.tag.assign(reinterpret_cast<char const*>(body.data()+i),len);i+=len;
   if(body.size()-i<12)return false;
   d.ctrl=get();d.wait_ctrl=get();d.data=get();
   s.devices.push_back(std::move(d));
  }
  if(i!=body.size())return false;
  out=std::move(s);return true;
 }
};
}
