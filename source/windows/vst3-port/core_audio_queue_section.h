#pragma once
#include "core_state_envelope.h"
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>
namespace eiii::vst3 {
struct CoreAudioQueueSection {
 struct State {std::vector<float> output;std::array<std::array<uint16_t,5>,16> cv{};};
 static constexpr uint32_t tag=0x31515541u;
 static constexpr size_t max_samples=1048576;
 static bool encode(State const& s,std::vector<uint8_t>& out){
  if(s.output.size()>max_samples||(s.output.size()&1))return false;
  std::vector<uint8_t> raw,wire;
  CoreStateEnvelope::put32(raw,tag);
  CoreStateEnvelope::put32(raw,uint32_t(s.output.size()));
  for(float value:s.output){
   if(!std::isfinite(value))return false;
   uint32_t bits=0;std::memcpy(&bits,&value,sizeof(bits));
   CoreStateEnvelope::put32(raw,bits);
  }
  for(auto const& voice:s.cv)
   for(uint16_t cv:voice){raw.push_back(uint8_t(cv));raw.push_back(uint8_t(cv>>8));}
  if(!CoreStateEnvelope::encode(raw,wire))return false;
  out.swap(wire);return true;
 }
 static bool decode(uint8_t const* p,size_t n,State& out){
  std::vector<uint8_t> raw;
  if(!CoreStateEnvelope::decode(p,n,raw)||raw.size()<8+160||
     CoreStateEnvelope::get32(raw.data())!=tag)return false;
  const uint32_t count=CoreStateEnvelope::get32(raw.data()+4);
  if(count>max_samples||(count&1)||raw.size()!=8+size_t(count)*4+160)return false;
  State candidate{};candidate.output.resize(count);size_t at=8;
  for(float& value:candidate.output){
   const uint32_t bits=CoreStateEnvelope::get32(raw.data()+at);at+=4;
   std::memcpy(&value,&bits,sizeof(bits));
   if(!std::isfinite(value))return false;
  }
  for(auto& voice:candidate.cv)
   for(uint16_t& cv:voice){cv=uint16_t(raw[at])|uint16_t(raw[at+1])<<8;at+=2;}
  out=std::move(candidate);return true;
 }
};
}
