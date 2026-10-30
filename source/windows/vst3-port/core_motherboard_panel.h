#pragma once
#include "core_state_envelope.h"
#include <array>
#include <cstdint>
#include <vector>
namespace eiii::vst3 {
struct CoreMotherboardPanel {
 struct State {
  uint16_t leds=0;
  uint8_t misc=0,fdc_status=0,extra_switches=0,adc_ctrl=0;
  std::array<uint8_t,16> scan_fifo{};
  uint32_t scan_head=0,scan_tail=0,scan_count=0;
  std::array<int16_t,2> adc_latch{};
  std::array<uint16_t,8> adc_pot{};
 };
 static constexpr uint32_t tag=0x314e504du;
 static bool valid(State const& s){
  return s.scan_head<16&&s.scan_tail<16&&s.scan_count<=16&&
    (s.scan_head+s.scan_count)%16==s.scan_tail;
 }
 static bool encode(State const& s,std::vector<uint8_t>& out){
  if(!valid(s))return false;
  std::vector<uint8_t> p,w;CoreStateEnvelope::put32(p,tag);
  CoreStateEnvelope::put32(p,s.leds);
  for(uint8_t x:{s.misc,s.fdc_status,s.extra_switches,s.adc_ctrl})p.push_back(x);
  p.insert(p.end(),s.scan_fifo.begin(),s.scan_fifo.end());
  for(uint32_t x:{s.scan_head,s.scan_tail,s.scan_count})CoreStateEnvelope::put32(p,x);
  for(int16_t x:s.adc_latch)CoreStateEnvelope::put32(p,uint16_t(x));
  for(uint16_t x:s.adc_pot)CoreStateEnvelope::put32(p,x);
  if(!CoreStateEnvelope::encode(p,w))return false;out.swap(w);return true;
 }
 static bool decode(uint8_t const* data,size_t size,State& out){
  std::vector<uint8_t> p;
  if(!CoreStateEnvelope::decode(data,size,p)||p.size()!=80||
      CoreStateEnvelope::get32(p.data())!=tag)return false;
  State s;size_t i=4;
  auto next=[&](){auto x=CoreStateEnvelope::get32(p.data()+i);i+=4;return x;};
  auto leds=next();if(leds>65535)return false;s.leds=uint16_t(leds);
  s.misc=p[i++];s.fdc_status=p[i++];s.extra_switches=p[i++];s.adc_ctrl=p[i++];
  for(auto& x:s.scan_fifo)x=p[i++];
  s.scan_head=next();s.scan_tail=next();s.scan_count=next();
  for(auto& x:s.adc_latch){auto v=next();if(v>65535)return false;x=int16_t(uint16_t(v));}
  for(auto& x:s.adc_pot){auto v=next();if(v>65535)return false;x=uint16_t(v);}
  if(!valid(s))return false;out=s;return true;
 }
};
}
