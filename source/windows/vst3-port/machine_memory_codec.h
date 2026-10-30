// B02: portable, bounded motherboard RAM/sample-memory snapshot payload.
// Not a complete machine save state; CPU/device/timer states are excluded.
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
namespace eiii::vst3 {
struct MachineMemoryCodec {
 static constexpr size_t ram_words=0x10000, sample_words=0x400000;
 static constexpr size_t payload_bytes=(ram_words+sample_words)*2;
 static constexpr size_t header_bytes=24;
 static constexpr uint32_t magic=0x324d3345u; // E3M2 little endian
 static constexpr uint32_t version=1;
 static void put32(std::vector<uint8_t>& out,uint32_t v) {
  for(unsigned i=0;i<4;++i)out.push_back(uint8_t(v>>(i*8)));
 }
 static uint32_t get32(const uint8_t* p) {
  return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);
 }
 static uint32_t hash(const uint8_t* p,size_t n) {
  uint32_t h=2166136261u;
  for(size_t i=0;i<n;++i) {h^=p[i];h*=16777619u;}
  return h;
 }
 static bool encode(const uint16_t* ram,size_t rn,const uint16_t* samples,size_t sn,
                    std::vector<uint8_t>& out) {
  if(!ram||!samples||rn!=ram_words||sn!=sample_words)return false;
  std::vector<uint8_t> tmp;
  tmp.reserve(header_bytes+payload_bytes);
  for(unsigned i=0;i<6;++i)put32(tmp,0);
  for(size_t i=0;i<rn;++i){tmp.push_back(uint8_t(ram[i]));tmp.push_back(uint8_t(ram[i]>>8));}
  for(size_t i=0;i<sn;++i){tmp.push_back(uint8_t(samples[i]));tmp.push_back(uint8_t(samples[i]>>8));}
  const uint32_t fields[]={magic,version,uint32_t(rn),uint32_t(sn),
                           hash(tmp.data()+header_bytes,payload_bytes),0};
  for(unsigned i=0;i<6;++i)
   for(unsigned k=0;k<4;++k)tmp[4*i+k]=uint8_t(fields[i]>>(8*k));
  out.swap(tmp);
  return true;
 }
 static bool decode(const uint8_t* bytes,size_t n,std::vector<uint16_t>& ram,
                    std::vector<uint16_t>& samples) {
  if(!bytes||n!=header_bytes+payload_bytes)return false;
  if(get32(bytes)!=magic||get32(bytes+4)!=version||
     get32(bytes+8)!=ram_words||get32(bytes+12)!=sample_words||
     get32(bytes+20)!=0||
     get32(bytes+16)!=hash(bytes+header_bytes,payload_bytes))return false;
  std::vector<uint16_t> r(ram_words),s(sample_words);
  const uint8_t* p=bytes+header_bytes;
  for(auto& word:r){word=uint16_t(p[0]|(uint16_t(p[1])<<8));p+=2;}
  for(auto& word:s){word=uint16_t(p[0]|(uint16_t(p[1])<<8));p+=2;}
  ram.swap(r);samples.swap(s);
  return true;
 }
};
}
