// B02 scheduler transport. SCH2 preserves the native time representation;
// the original SCH1 binary64 snapshots remain readable without conversion loss.
#pragma once
#include "core_time.h"
namespace eiii::vst3 {
struct CoreSchedulerSection {
 struct Timer {long double expire=0,start=0,period=0;int32_t param=0;bool enabled=false;};
 struct State {long double basetime=0;std::vector<Timer> timers;};
 static constexpr uint32_t tag=0x32484353u,legacy_tag=0x31484353u,max_timers=65536;
 static bool encode(State const& s,std::vector<uint8_t>& out) {
  if(!CoreTime::valid(s.basetime)||s.basetime>=1e29||s.timers.size()>max_timers)return false;
  std::vector<uint8_t> body;
  CoreStateEnvelope::put32(body,tag);CoreStateEnvelope::put32(body,uint32_t(s.timers.size()));
  if(!CoreTime::put(body,s.basetime))return false;
  for(auto const& t:s.timers){
   if(!CoreTime::put(body,t.expire)||!CoreTime::put(body,t.start)||!CoreTime::put(body,t.period))return false;
   CoreStateEnvelope::put32(body,uint32_t(t.param));CoreStateEnvelope::put32(body,t.enabled?1:0);
  }
  return CoreStateEnvelope::encode(body,out);
 }
 static bool decode(uint8_t const* p,size_t n,State& out) {
  std::vector<uint8_t> body;
  if(!CoreStateEnvelope::decode(p,n,body)||body.size()<16)return false;
  uint32_t format=CoreStateEnvelope::get32(body.data());
  if(format!=tag&&format!=legacy_tag)return false;
  size_t width=format==tag?16:8;
  uint32_t count=CoreStateEnvelope::get32(body.data()+4);
  if(count>max_timers||body.size()!=8+width+size_t(count)*(3*width+8))return false;
  size_t i=8;
  auto time=[&](long double& t){
   bool ok=true;
   if(width==16)ok=CoreTime::get(body.data()+i,t);
   else {t=CoreTime::get_double(body.data()+i);ok=CoreTime::valid(t);}
   i+=width;return ok;
  };
  State s;if(!time(s.basetime)||s.basetime>=1e29)return false;
  s.timers.reserve(count);
  for(uint32_t k=0;k<count;++k){
   Timer t;if(!time(t.expire)||!time(t.start)||!time(t.period))return false;
   t.param=int32_t(CoreStateEnvelope::get32(body.data()+i));i+=4;
   uint32_t en=CoreStateEnvelope::get32(body.data()+i);i+=4;
   if(en>1)return false;t.enabled=en!=0;s.timers.push_back(t);
  }
  out=std::move(s);return true;
 }
};
}
