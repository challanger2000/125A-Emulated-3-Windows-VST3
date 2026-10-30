// Versioned, bounded multi-section container for a future complete B02 snapshot.
// Section IDs are canonical and duplicate sections are always rejected.
#pragma once
#include "core_state_envelope.h"
#include <cstdint>
#include <vector>
#include <utility>
namespace eiii::vst3 {
struct CoreSectionContainer {
 struct Section { uint32_t id=0;std::vector<uint8_t> bytes; };
 static constexpr uint32_t tag=0x32434553u; // SEC2
 static constexpr uint32_t max_sections=64;
 static bool encode(std::vector<Section> const& sections,std::vector<uint8_t>& out) {
  if(sections.empty()||sections.size()>max_sections)return false;
  std::vector<uint8_t> payload,wire;
  CoreStateEnvelope::put32(payload,tag);
  CoreStateEnvelope::put32(payload,uint32_t(sections.size()));
  uint32_t previous=0;
  for(auto const& s:sections){
   if(s.id<=previous||s.bytes.empty()||
      s.bytes.size()>CoreStateEnvelope::max_payload ||
      payload.size()>CoreStateEnvelope::max_payload-8 ||
      s.bytes.size()>CoreStateEnvelope::max_payload-8-payload.size())return false;
   previous=s.id;
   CoreStateEnvelope::put32(payload,s.id);
   CoreStateEnvelope::put32(payload,uint32_t(s.bytes.size()));
   payload.insert(payload.end(),s.bytes.begin(),s.bytes.end());
  }
  if(!CoreStateEnvelope::encode(payload,wire))return false;
  out.swap(wire);return true;
 }
 static bool decode(uint8_t const* data,size_t n,std::vector<Section>& out) {
  std::vector<uint8_t> payload;
  if(!CoreStateEnvelope::decode(data,n,payload)||payload.size()<8||
     CoreStateEnvelope::get32(payload.data())!=tag)return false;
  uint32_t count=CoreStateEnvelope::get32(payload.data()+4);
  if(count==0||count>max_sections)return false;
  std::vector<Section> staged;staged.reserve(count);
  size_t i=8;uint32_t previous=0;
  for(uint32_t k=0;k<count;++k){
   if(i>payload.size()||payload.size()-i<8)return false;
   uint32_t id=CoreStateEnvelope::get32(payload.data()+i);
   uint32_t len=CoreStateEnvelope::get32(payload.data()+i+4);i+=8;
   if(id<=previous||len==0||len>payload.size()-i)return false;
   previous=id;Section section;section.id=id;
   section.bytes.assign(payload.begin()+i,payload.begin()+i+len);
   staged.push_back(std::move(section));i+=len;
  }
  if(i!=payload.size())return false;
  out.swap(staged);return true;
 }
};
}
