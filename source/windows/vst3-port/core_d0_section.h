// 125A B02 D0/FDC checkpoint transport. No MAME pointer, callback, or
// floppy-media data crosses the VST3 state boundary. All fields are canonical,
// bounded LE scalars and lossless CoreTime values; outer checksum rejects damage.
#pragma once
#include "core_time.h"
#include "core_d0_format.h"
#include "emu.h"
#include "machine/wd_fdc.h"
#include "imagedev/floppy.h"
#include <array>
#include <bit>
#include <string>
namespace eiii::vst3 {
struct CoreD0Section {
 struct State {
  floppy_image_device::recall_state drive{};
  wd1772_device::recall_state controller{};
 };
 static constexpr uint32_t tag=CoreD0Format::tag;
 static constexpr uint32_t version=CoreD0Format::version;
 static constexpr size_t max_format=4096;
 static void put64(std::vector<uint8_t>& b,uint64_t v) {
  CoreStateEnvelope::put32(b,uint32_t(v));
  CoreStateEnvelope::put32(b,uint32_t(v>>32));
 }
 static bool encode(State const& s,std::vector<uint8_t>& out) {
  std::vector<uint8_t> b;
  b.reserve(2048+s.controller.controller.format_description_string.size());
  auto u=[&](uint32_t n){CoreStateEnvelope::put32(b,n);};
  auto si=[&](int32_t n){u(std::bit_cast<uint32_t>(n));};
  auto tm=[&](attotime a){return CoreTime::put(b,a.m_seconds);};
  u(tag);u(version);
  auto const& d=s.drive;
  u(uint32_t(d.loaded)|(uint32_t(d.motor)<<1)|
    (uint32_t(d.index)<<2)|(uint32_t(d.step)<<3));
  si(d.cylinder);si(d.side);si(d.direction);
  if(!tm(d.origin))return false;
  put64(b,d.media_hash);
  auto const& c=s.controller.controller;
  u(uint32_t(c.dden)|(uint32_t(c.status_type_1)<<1)|
    (uint32_t(c.intrq)<<2)|(uint32_t(c.drq)<<3)|
    (uint32_t(c.hld)<<4)|(uint32_t(c.hlt)<<5)|
    (uint32_t(c.enp)<<6)|(uint32_t(c.mr)<<7)|
    (uint32_t(c.enmf)<<8)|(uint32_t(c.force_ready)<<9)|
    (uint32_t(c.delay_int)<<10)|(uint32_t(c.delay_cmd)<<11));
  for(int32_t x:std::array<int32_t,10>{
    c.main_state,c.sub_state,c.last_dir,c.counter,c.motor_timeout,c.sector_size,
    c.cmd_buffer,c.track_buffer,c.sector_buffer,c.format_last_byte_count})si(x);
  for(uint32_t x:std::array<uint32_t,7>{
    c.command,c.track,c.sector,c.data,c.status,c.intrq_cond,c.format_last_byte})u(x);
  auto live=[&](wd_fdc_device_base::live_recall_state const& x)->bool {
   if(!tm(x.tm))return false;
   si(x.state);si(x.next_state);u(x.shift_reg);u(x.crc);
   si(x.bit_counter);si(x.byte_counter);si(x.previous_type);
   u(uint32_t(x.data_separator_phase)|(uint32_t(x.data_bit_context)<<1));
   u(x.data_reg);for(uint8_t v:x.idbuf)u(v);
   return true;
  };
  if(!live(c.live)||!live(c.checkpoint))return false;
  auto pll=[&](wd_fdc_digital_device_base::pll_recall_state const& x)->bool {
   for(uint32_t n:std::array<uint32_t,9>{
     x.counter,x.increment,x.transition_time,x.history,x.slot,
     x.phase_add,x.phase_sub,x.freq_add,x.freq_sub})u(n);
   if(!tm(x.ctime))return false;
   for(attotime t:x.delays)if(!tm(t))return false;
   return true;
  };
  if(!pll(s.controller.current)||!pll(s.controller.checkpoint))return false;
  auto const& str=c.format_description_string;
  if(str.size()>max_format)return false;
  u(uint32_t(str.size()));b.insert(b.end(),str.begin(),str.end());
  return CoreStateEnvelope::encode(b,out);
 }
 static bool decode(uint8_t const* src,size_t n,State& out) {
  std::vector<uint8_t> b;
  if(!CoreStateEnvelope::decode(src,n,b))return false;
  size_t p=0;
  auto u=[&](uint32_t& x)->bool {
   if(b.size()-p<4)return false;
   x=CoreStateEnvelope::get32(b.data()+p);p+=4;return true;
  };
  auto si=[&](int& x)->bool {
   uint32_t v=0;if(!u(v))return false;x=std::bit_cast<int32_t>(v);return true;
  };
  auto tm=[&](attotime& x)->bool {
   if(b.size()-p<CoreTime::bytes)return false;
   long double t=0;
   if(!CoreTime::get(b.data()+p,t))return false;
   p+=CoreTime::bytes;x=attotime(t);return true;
  };
  auto byte=[&](uint8_t& x)->bool {
   uint32_t v=0;if(!u(v)||v>255)return false;x=uint8_t(v);return true;
  };
  uint32_t v=0,w=0;
  if(!u(v)||v!=tag||!u(v)||v!=version)return false;
  State s;
  auto& d=s.drive;
  if(!u(v)||v&~uint32_t(15))return false;
  d.loaded=(v&1)!=0;d.motor=(v&2)!=0;d.index=(v&4)!=0;d.step=(v&8)!=0;
  if(!si(d.cylinder)||!si(d.side)||!si(d.direction)||!tm(d.origin)||
     !u(v)||!u(w))return false;
  d.media_hash=uint64_t(v)|(uint64_t(w)<<32);
  auto& c=s.controller.controller;
  if(!u(v)||v&~uint32_t(4095))return false;
  c.dden=v&1;c.status_type_1=v&2;c.intrq=v&4;c.drq=v&8;
  c.hld=v&16;c.hlt=v&32;c.enp=v&64;c.mr=v&128;
  c.enmf=v&256;c.force_ready=v&512;c.delay_int=v&1024;c.delay_cmd=v&2048;
  for(int* x:std::array<int*,10>{&c.main_state,&c.sub_state,&c.last_dir,
      &c.counter,&c.motor_timeout,&c.sector_size,&c.cmd_buffer,&c.track_buffer,
      &c.sector_buffer,&c.format_last_byte_count})
   if(!si(*x))return false;
  for(uint8_t* x:std::array<uint8_t*,7>{&c.command,&c.track,&c.sector,&c.data,
     &c.status,&c.intrq_cond,&c.format_last_byte})if(!byte(*x))return false;
  auto live=[&](wd_fdc_device_base::live_recall_state& x)->bool {
   if(!tm(x.tm)||!si(x.state)||!si(x.next_state)||!u(x.shift_reg)||
      !u(v)||v>65535)return false;x.crc=uint16_t(v);
   if(!si(x.bit_counter)||!si(x.byte_counter)||!si(x.previous_type)||
      !u(v)||v&~uint32_t(3))return false;
   x.data_separator_phase=v&1;x.data_bit_context=v&2;
   if(!byte(x.data_reg))return false;
   for(uint8_t& q:x.idbuf)if(!byte(q))return false;
   return true;
  };
  if(!live(c.live)||!live(c.checkpoint))return false;
  auto pll=[&](wd_fdc_digital_device_base::pll_recall_state& x)->bool {
   std::array<uint32_t,9> fields{};
   for(auto& q:fields)if(!u(q))return false;
   for(auto q:fields)if(q>65535)return false;
   x.counter=uint16_t(fields[0]);x.increment=uint16_t(fields[1]);
   x.transition_time=uint16_t(fields[2]);
   x.history=uint8_t(fields[3]);x.slot=uint8_t(fields[4]);
   x.phase_add=uint8_t(fields[5]);x.phase_sub=uint8_t(fields[6]);
   x.freq_add=uint8_t(fields[7]);x.freq_sub=uint8_t(fields[8]);
   if(fields[3]>255||fields[4]>255||fields[5]>255||fields[6]>255||
      fields[7]>255||fields[8]>255||!tm(x.ctime))return false;
   for(auto& t:x.delays)if(!tm(t))return false;
   return true;
  };
  if(!pll(s.controller.current)||!pll(s.controller.checkpoint))return false;
  if(!u(v)||v>max_format||v>b.size()-p)return false;
  c.format_description_string.assign(reinterpret_cast<char const*>(b.data()+p),v);
  p+=v;
  if(p!=b.size())return false;
  out=std::move(s);
  return true;
 }
};
}
