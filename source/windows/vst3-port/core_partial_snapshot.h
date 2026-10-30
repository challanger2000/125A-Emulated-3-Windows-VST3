// B02 composite transport for RAM and MIDI. NOT a full machine snapshot.
#pragma once
#include "core_memory_section.h"
#include "core_midi_section.h"
#include <vector>
#include <cstdint>
namespace eiii::vst3 {
struct CorePartialSnapshot {
 static constexpr uint32_t tag=0x31504345u; // ECP1 little endian
 static bool encode(const uint16_t* ram,size_t rn,const uint16_t* sample,size_t sn,
                    CoreMidiSection::State const& midi,std::vector<uint8_t>& out) {
  std::vector<uint8_t> mem,events,body,result;
  if(!CoreMemorySection::encode(ram,rn,sample,sn,mem) ||
     !CoreMidiSection::encode(midi,events) ||
     mem.size()>UINT32_MAX||events.size()>UINT32_MAX)return false;
  if(mem.size()+events.size()+12>CoreStateEnvelope::max_payload)return false;
  CoreStateEnvelope::put32(body,tag);
  CoreStateEnvelope::put32(body,uint32_t(mem.size()));
  CoreStateEnvelope::put32(body,uint32_t(events.size()));
  body.insert(body.end(),mem.begin(),mem.end());
  body.insert(body.end(),events.begin(),events.end());
  if(!CoreStateEnvelope::encode(body,result))return false;
  out.swap(result);return true;
 }
 static bool decode(const uint8_t* bytes,size_t n,std::vector<uint16_t>& ram,
                    std::vector<uint16_t>& sample,CoreMidiSection::State& midi) {
  std::vector<uint8_t> body;
  if(!CoreStateEnvelope::decode(bytes,n,body)||body.size()<12||
     CoreStateEnvelope::get32(body.data())!=tag)return false;
  const size_t mem=CoreStateEnvelope::get32(body.data()+4);
  const size_t events=CoreStateEnvelope::get32(body.data()+8);
  if(mem>body.size()-12 || events!=body.size()-12-mem)return false;
  std::vector<uint16_t> r,s;
  CoreMidiSection::State m;
  if(!CoreMemorySection::decode(body.data()+12,mem,r,s) ||
     !CoreMidiSection::decode(body.data()+12+mem,events,m))return false;
  ram.swap(r);sample.swap(s);midi=std::move(m);return true;
 }
};
}
