#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <vector>
#include "../vst3-port/memory_capture.h"
struct FakeEngine {
 bool ok=true, wrong_size=false;
 bool capture_volatile_memory(std::vector<uint16_t>& r,std::vector<uint16_t>& s) {
  if(!ok)return false;
  r.assign(eiii::vst3::MachineMemoryCodec::ram_words,0x1278);
  s.assign(eiii::vst3::MachineMemoryCodec::sample_words,0x34ab);
  if(wrong_size)r.pop_back();
  return true;
 }
};
int main() {
 using namespace eiii::vst3;
 FakeEngine e;
 std::vector<uint8_t> encoded{5,6,7};
 e.ok=false;
 assert(!MemoryCapture::capture(e,encoded));
 assert((encoded==std::vector<uint8_t>{5,6,7}));
 e.ok=true;e.wrong_size=true;
 assert(!MemoryCapture::capture(e,encoded));
 assert((encoded==std::vector<uint8_t>{5,6,7}));
 e.wrong_size=false;
 assert(MemoryCapture::capture(e,encoded));
 std::vector<uint16_t> r,s;
 assert(MachineMemoryCodec::decode(encoded.data(),encoded.size(),r,s));
 assert(r.front()==0x1278 && r.back()==0x1278);
 assert(s.front()==0x34ab && s.back()==0x34ab);
}
