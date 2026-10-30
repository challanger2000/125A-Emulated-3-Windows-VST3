// B02: portable MIDI transmitter subsection. No device pointers.
#pragma once
#include <cstdint>
#include <cstring>
#include <cmath>
#include <vector>
#include <utility>
namespace eiii::vst3 {
struct CoreMidiSection {
 struct Event { double time=0; std::vector<uint8_t> bytes; };
 struct State { std::vector<Event> events; uint32_t next=0; std::vector<uint8_t> queue; uint32_t head=0; int32_t bit=-1; uint8_t current=0; };
 static constexpr uint32_t magic=0x3144494du, max_events=100000, max_bytes=16u*1024u*1024u;
 static void u32(std::vector<uint8_t>& v,uint32_t n){ for(unsigned k=0;k<4;++k)v.push_back(uint8_t(n>>(8*k))); }
 static bool read(uint8_t const* p,size_t n,size_t& i,uint32_t& v) {
  if(i>n||n-i<4)return false;
  v=uint32_t(p[i])|(uint32_t(p[i+1])<<8)|(uint32_t(p[i+2])<<16)|(uint32_t(p[i+3])<<24);i+=4;return true;
 }
 static bool encode(State const& s,std::vector<uint8_t>& out) {
  if(s.events.size()>max_events||s.next>s.events.size()||s.queue.size()>max_bytes||
     s.head>s.queue.size()||s.bit < -1||s.bit>8)return false;
  std::vector<uint8_t> tmp;tmp.reserve(64+s.queue.size());
  u32(tmp,magic);u32(tmp,uint32_t(s.events.size()));u32(tmp,s.next);
  u32(tmp,uint32_t(s.queue.size()));u32(tmp,s.head);
  u32(tmp,uint32_t(s.bit));u32(tmp,s.current);
  tmp.insert(tmp.end(),s.queue.begin(),s.queue.end());
  double last=-1;
  for(auto const& e:s.events) {
   if(!std::isfinite(e.time)||e.time<0||e.time<last||e.bytes.size()>max_bytes||
      tmp.size()+12+e.bytes.size()>max_bytes)return false;
   last=e.time;
   uint64_t bits=0;std::memcpy(&bits,&e.time,8);
   u32(tmp,uint32_t(bits));u32(tmp,uint32_t(bits>>32));
   u32(tmp,uint32_t(e.bytes.size()));tmp.insert(tmp.end(),e.bytes.begin(),e.bytes.end());
  }
  if(tmp.size()>max_bytes)return false;
  out.swap(tmp);return true;
 }
 static bool decode(uint8_t const* p,size_t n,State& out) {
  if(!p||n<28||n>max_bytes)return false;
  size_t i=0;uint32_t tag,count,next,qlen,head,bit,current;
  if(!read(p,n,i,tag)||!read(p,n,i,count)||!read(p,n,i,next)||
     !read(p,n,i,qlen)||!read(p,n,i,head)||!read(p,n,i,bit)||
     !read(p,n,i,current)||tag!=magic||count>max_events||next>count||
     qlen>n-i||head>qlen||bit>8&&bit!=UINT32_MAX||current>255)return false;
  State s;s.next=next;s.head=head;s.bit=bit==UINT32_MAX?-1:int32_t(bit);s.current=uint8_t(current);
  s.queue.assign(p+i,p+i+qlen);i+=qlen;
  double last=-1;
  for(uint32_t k=0;k<count;++k){
   uint32_t lo,hi,len;
   if(!read(p,n,i,lo)||!read(p,n,i,hi)||!read(p,n,i,len)||len>n-i)return false;
   uint64_t bits=uint64_t(lo)|(uint64_t(hi)<<32);double t;
   std::memcpy(&t,&bits,8);
   if(!std::isfinite(t)||t<0||t<last)return false;
   last=t;Event e;e.time=t;e.bytes.assign(p+i,p+i+len);i+=len;s.events.push_back(std::move(e));
  }
  if(i!=n)return false;
  out=std::move(s);return true;
 }
};
}
