// B02 portable envelope for future full-device machine snapshots.
// Transport only. A valid envelope is NOT proof that its payload is complete.
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
#include <utility>
namespace eiii::vst3 {
struct CoreStateEnvelope {
 static constexpr uint32_t magic=0x53334945u; // EI3S, LE
 static constexpr uint32_t version=1;
 static constexpr size_t header_size=20;
 static constexpr size_t max_payload=192u*1024u*1024u;
 static void put32(std::vector<uint8_t>& dst,uint32_t v) {
  for(unsigned i=0;i<4;++i)dst.push_back(uint8_t(v>>(i*8)));
 }
 static uint32_t get32(uint8_t const* p) {
  return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);
 }
 static uint32_t checksum(uint8_t const* p,size_t n) {
  uint32_t h=2166136261u;
  for(size_t i=0;i<n;++i){h^=p[i];h*=16777619u;}
  return h;
 }
 static bool encode(std::vector<uint8_t> const& payload,std::vector<uint8_t>& out) {
  if(payload.empty()||payload.size()>max_payload)return false;
  std::vector<uint8_t> result;
  result.reserve(header_size+payload.size());
  put32(result,magic);put32(result,version);
  put32(result,uint32_t(payload.size()));
  put32(result,checksum(payload.data(),payload.size()));
  put32(result,0); // reserved flags must be zero for this version
  result.insert(result.end(),payload.begin(),payload.end());
  out.swap(result);return true;
 }
 static bool decode(uint8_t const* bytes,size_t size,std::vector<uint8_t>& out) {
  if(!bytes||size<header_size||size>header_size+max_payload)return false;
  if(get32(bytes)!=magic||get32(bytes+4)!=version||
     get32(bytes+16)!=0||get32(bytes+8)!=size-header_size ||
     size==header_size||get32(bytes+12)!=checksum(bytes+header_size,size-header_size))return false;
  std::vector<uint8_t> result(bytes+header_size,bytes+size);
  out.swap(result);return true;
 }
};
}
