// B02 partial memory snapshot acquisition. Not a complete machine save state.
#pragma once
#include "machine_memory_codec.h"
#include <cstdint>
#include <vector>
namespace eiii::vst3 {
struct MemoryCapture {
 template<class Engine>
 static bool capture(Engine& engine,std::vector<uint8_t>& output) {
  std::vector<uint16_t> ram,samples;
  if(!engine.capture_volatile_memory(ram,samples))return false;
  std::vector<uint8_t> encoded;
  if(!MachineMemoryCodec::encode(ram.data(),ram.size(),
                                 samples.data(),samples.size(),encoded))return false;
  output.swap(encoded);
  return true;
 }
};
}
