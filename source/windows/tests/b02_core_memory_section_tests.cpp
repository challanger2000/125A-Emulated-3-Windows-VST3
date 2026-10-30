#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdint>
#include <vector>
#include "../vst3-port/core_memory_section.h"
int main() {
 using eiii::vst3::CoreMemorySection;
 std::vector<uint16_t> ram(0x10000),samples(0x400000);
 for(size_t i=0;i<ram.size();++i)ram[i]=uint16_t(i*17u);
 for(size_t i=0;i<samples.size();++i)samples[i]=uint16_t(i*31u);
 std::vector<uint8_t> envelope;
 assert(CoreMemorySection::encode(ram.data(),ram.size(),samples.data(),samples.size(),envelope));
 std::vector<uint16_t> restoredRam,restoredSamples;
 assert(CoreMemorySection::decode(envelope.data(),envelope.size(),restoredRam,restoredSamples));
 assert(restoredRam==ram && restoredSamples==samples);
 envelope.back()^=1;
 assert(!CoreMemorySection::decode(envelope.data(),envelope.size(),restoredRam,restoredSamples));
 assert(restoredRam==ram && restoredSamples==samples);
 envelope.back()^=1;
 envelope[16]=1;
 assert(!CoreMemorySection::decode(envelope.data(),envelope.size(),restoredRam,restoredSamples));
 assert(restoredRam==ram && restoredSamples==samples);
}
