// B02 portable RAM subsection of a future complete machine-state stream.
// Explicitly NOT sufficient to restore CPU, timers, media or DSP devices.
#pragma once
#include "core_state_envelope.h"
#include "machine_memory_codec.h"
#include <vector>
#include <cstdint>
namespace eiii::vst3 {
struct CoreMemorySection {
 static constexpr uint32_t tag=0x314d4252u; // RBM1, little endian
 static bool encode(uint16_t const* ram,size_t rn,uint16_t const* sample,size_t sn,
                    std::vector<uint8_t>& bytes) {
  std::vector<uint8_t> memory, payload, result;
  if(!MachineMemoryCodec::encode(ram,rn,sample,sn,memory))return false;
  payload.reserve(4+memory.size());
  CoreStateEnvelope::put32(payload,tag);
  payload.insert(payload.end(),memory.begin(),memory.end());
  if(!CoreStateEnvelope::encode(payload,result))return false;
  bytes.swap(result);return true;
 }
 static bool decode(uint8_t const* data,size_t size,
                    std::vector<uint16_t>& ram,std::vector<uint16_t>& sample) {
  std::vector<uint8_t> payload;
  if(!CoreStateEnvelope::decode(data,size,payload) ||
     payload.size()!=4+MachineMemoryCodec::header_bytes+MachineMemoryCodec::payload_bytes ||
     CoreStateEnvelope::get32(payload.data())!=tag)return false;
  return MachineMemoryCodec::decode(payload.data()+4,payload.size()-4,ram,sample);
 }
};
}
