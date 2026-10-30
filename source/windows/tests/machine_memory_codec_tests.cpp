#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <vector>
#include "../vst3-port/machine_memory_codec.h"
int main() {
 using C=eiii::vst3::MachineMemoryCodec;
 std::vector<uint16_t> ram(C::ram_words),samples(C::sample_words);
 for(size_t i=0;i<ram.size();++i)ram[i]=uint16_t(i*17+7);
 for(size_t i=0;i<samples.size();++i)samples[i]=uint16_t(i*7919+123);
 std::vector<uint8_t> bytes;
 assert(C::encode(ram.data(),ram.size(),samples.data(),samples.size(),bytes));
 assert(bytes.size()==C::header_bytes+C::payload_bytes);
 std::vector<uint16_t> r,s;
 assert(C::decode(bytes.data(),bytes.size(),r,s));
 assert(r==ram&&s==samples);
 auto previousR=r,previousS=s;
 bytes.back()^=0x1;
 assert(!C::decode(bytes.data(),bytes.size(),r,s));
 assert(r==previousR&&s==previousS);
 bytes.back()^=0x1;
 assert(!C::decode(bytes.data(),bytes.size()-1,r,s));
 bytes[4]=0xff;
 assert(!C::decode(bytes.data(),bytes.size(),r,s));
 assert(!C::encode(ram.data(),ram.size()-1,samples.data(),samples.size(),bytes));
 assert(!C::encode(ram.data(),ram.size(),nullptr,samples.size(),bytes));
}
