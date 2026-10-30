#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include "../vst3-port/core_audio_timing.h"
int main(){
 using C=eiii::vst3::CoreAudioTiming;
 C::State original{},loaded{};
 original.values={44100,176400,12345.125,-.25,.5};
 original.sample_words=4194304;original.now=987654321;original.output_frames=0x123456789ULL;
 original.oversample=4;original.phase=3;original.model_v2=true;
 std::vector<uint8_t> bytes;
 assert(C::encode(original,bytes));
 assert(C::decode(bytes.data(),bytes.size(),loaded));
 assert(loaded.now==original.now&&loaded.values==original.values);
 assert(loaded.phase==3&&loaded.model_v2&&loaded.output_frames==original.output_frames);
 auto corrupted=bytes;corrupted.back()^=1;
 assert(!C::decode(corrupted.data(),corrupted.size(),loaded));
 assert(loaded.now==original.now);
 auto invalid=original;invalid.phase=4;
 assert(!C::encode(invalid,bytes));
}
