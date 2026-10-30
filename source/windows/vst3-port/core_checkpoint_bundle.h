// B02 transport for RAM/MIDI plus CPU position. Not a full machine snapshot.
#pragma once
#include "core_partial_snapshot.h"
#include "core_execution_section.h"
#include <vector>
#include <cstdint>
namespace eiii::vst3 {
struct CoreCheckpointBundle {
 static constexpr uint32_t tag=0x31424b43u; // CKB1
 static bool encode(const uint16_t* ram,size_t rn,const uint16_t* sample,size_t sn,
                    CoreMidiSection::State const& midi,
                    CoreExecutionSection::State const& cpu,std::vector<uint8_t>& out) {
  std::vector<uint8_t> partial,execution,body,wire;
  if(!CorePartialSnapshot::encode(ram,rn,sample,sn,midi,partial) ||
     !CoreExecutionSection::encode(cpu,execution))return false;
  if(partial.size()>CoreStateEnvelope::max_payload-12 ||
     execution.size()>CoreStateEnvelope::max_payload-12-partial.size())return false;
  CoreStateEnvelope::put32(body,tag);
  CoreStateEnvelope::put32(body,uint32_t(partial.size()));
  CoreStateEnvelope::put32(body,uint32_t(execution.size()));
  body.insert(body.end(),partial.begin(),partial.end());
  body.insert(body.end(),execution.begin(),execution.end());
  if(!CoreStateEnvelope::encode(body,wire))return false;
  out.swap(wire);return true;
 }
 static bool decode(const uint8_t* p,size_t n,std::vector<uint16_t>& ram,
                    std::vector<uint16_t>& sample,CoreMidiSection::State& midi,
                    CoreExecutionSection::State& cpu) {
  std::vector<uint8_t> body;
  if(!CoreStateEnvelope::decode(p,n,body)||body.size()<12||
     CoreStateEnvelope::get32(body.data())!=tag)return false;
  size_t a=CoreStateEnvelope::get32(body.data()+4);
  size_t b=CoreStateEnvelope::get32(body.data()+8);
  if(a>body.size()-12||b!=body.size()-12-a)return false;
  std::vector<uint16_t> r,s;CoreMidiSection::State m;
  CoreExecutionSection::State e;
  if(!CorePartialSnapshot::decode(body.data()+12,a,r,s,m) ||
     !CoreExecutionSection::decode(body.data()+12+a,b,e))return false;
  ram.swap(r);sample.swap(s);midi=std::move(m);cpu=e;return true;
 }
};
}
