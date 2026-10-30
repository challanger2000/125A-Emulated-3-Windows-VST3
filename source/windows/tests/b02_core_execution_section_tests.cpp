#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdint>
#include <vector>
#include "../vst3-port/core_execution_section.h"
int main(){
 using C=eiii::vst3::CoreExecutionSection;
 C::State original{0x12345678,0xFEDCBA9876543210ULL}, decoded{};
 std::vector<uint8_t> wire;
 assert(C::encode(original,wire));
 assert(C::decode(wire.data(),wire.size(),decoded));
 assert(decoded.pc==original.pc&&decoded.completed_cycles==original.completed_cycles);
 const auto saved=decoded;
 wire[20]^=0xff;
 assert(!C::decode(wire.data(),wire.size(),decoded));
 assert(decoded.pc==saved.pc&&decoded.completed_cycles==saved.completed_cycles);
 wire[20]^=0xff;
 assert(!C::decode(wire.data(),wire.size()-1,decoded));
 assert(decoded.pc==saved.pc&&decoded.completed_cycles==saved.completed_cycles);
}
