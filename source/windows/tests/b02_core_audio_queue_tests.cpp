#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <limits>
#include "../vst3-port/core_audio_queue_section.h"
int main(){
 using C=eiii::vst3::CoreAudioQueueSection;
 C::State a{},b{};
 a.output={.25f,-.5f,0.0f,1.0f};a.cv[3][4]=65535;
 std::vector<uint8_t> bytes;
 assert(C::encode(a,bytes));
 assert(C::decode(bytes.data(),bytes.size(),b));
 assert(b.output==a.output&&b.cv==a.cv);
 auto corrupted=bytes;corrupted[corrupted.size()-1]^=1;
 assert(!C::decode(corrupted.data(),corrupted.size(),b));
 assert(b.output==a.output);
 a.output.push_back(0.5f);assert(!C::encode(a,bytes));
 a.output.pop_back();
 a.output[0]=std::numeric_limits<float>::quiet_NaN();
 assert(!C::encode(a,bytes));
}
