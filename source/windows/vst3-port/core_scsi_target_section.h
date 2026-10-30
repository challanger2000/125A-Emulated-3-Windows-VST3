// B02 lossless wire format for common SCSI target execution/queue state.
// HDD and CD each embed this state; media identity is encoded separately.
#pragma once
#include "core_state_envelope.h"
#include <array>
#include <cstdint>
#include <vector>
namespace eiii::vst3 {
struct CoreScsiTargetSection {
 struct Action {int32_t action=0,param1=0,param2=0;};
 struct State {
  std::array<uint8_t,4096> command{};
  std::array<uint8_t,18> sense{};
  int32_t command_size=0;uint8_t identify=0;
  int32_t state=0,substate=0,initiator_id=-1;
  int32_t buffer_id=0,buffer_size=0,buffer_pos=0;
  std::array<Action,32> actions{};
  int32_t queue_read=0,queue_write=0;
 };
 static constexpr uint32_t tag=0x31544753u; // SGT1
 static constexpr size_t payload_size=4+4096+18+4+4+6*4+32*12+8;
 static bool encode(State const& s,std::vector<uint8_t>& out){
  if(s.command_size<0||s.command_size>4096||
     s.queue_read<0||s.queue_write<0||s.queue_read>s.queue_write||s.queue_write>32)return false;
  std::vector<uint8_t> b,w;CoreStateEnvelope::put32(b,tag);
  b.insert(b.end(),s.command.begin(),s.command.end());
  b.insert(b.end(),s.sense.begin(),s.sense.end());
  CoreStateEnvelope::put32(b,uint32_t(s.command_size));
  CoreStateEnvelope::put32(b,s.identify);
  for(int32_t v:{s.state,s.substate,s.initiator_id,s.buffer_id,s.buffer_size,s.buffer_pos})
   CoreStateEnvelope::put32(b,uint32_t(v));
  for(auto const& a:s.actions){
   CoreStateEnvelope::put32(b,uint32_t(a.action));CoreStateEnvelope::put32(b,uint32_t(a.param1));
   CoreStateEnvelope::put32(b,uint32_t(a.param2));
  }
  CoreStateEnvelope::put32(b,uint32_t(s.queue_read));
  CoreStateEnvelope::put32(b,uint32_t(s.queue_write));
  if(b.size()!=payload_size||!CoreStateEnvelope::encode(b,w))return false;
  out.swap(w);return true;
 }
 static bool decode(uint8_t const* p,size_t n,State& out){
  std::vector<uint8_t> b;
  if(!CoreStateEnvelope::decode(p,n,b)||b.size()!=payload_size||
     CoreStateEnvelope::get32(b.data())!=tag)return false;
  State s;size_t at=4;
  for(auto& v:s.command)v=b[at++];
  for(auto& v:s.sense)v=b[at++];
  auto get=[&](){auto v=CoreStateEnvelope::get32(b.data()+at);at+=4;return v;};
  s.command_size=int32_t(get());auto id=get();if(id>255)return false;s.identify=uint8_t(id);
  s.state=int32_t(get());s.substate=int32_t(get());s.initiator_id=int32_t(get());
  s.buffer_id=int32_t(get());s.buffer_size=int32_t(get());s.buffer_pos=int32_t(get());
  for(auto& a:s.actions){a.action=int32_t(get());a.param1=int32_t(get());a.param2=int32_t(get());}
  s.queue_read=int32_t(get());s.queue_write=int32_t(get());
  if(s.command_size<0||s.command_size>4096||
     s.queue_read<0||s.queue_write<0||s.queue_read>s.queue_write||s.queue_write>32)return false;
  out=std::move(s);return true;
 }
};
}
