#ifdef NDEBUG
#undef NDEBUG
#endif
#include "../../upstream/runtime/eiii_runtime_impl.h"
#include <cassert>
int main(){
 using Core=eiii::runtime::impl;
 std::vector<uint16_t> ram(65536);
 for(double rate:{44100.,48000.})for(bool v2:{false,true}){
  eiii::software_backend backend(ram.data(),ram.size(),rate,2);
  if(v2){eiii::cem_calibration calibration;backend.use_model_v2(calibration,4);}
  for(uint64_t ticks:{1ull,17ull,1001ull,1000001ull,20000001ull}){
   backend.render_until(ticks);eiii::software_backend::recall_state state{};
   assert(backend.capture_recall_state(state));
   assert(Core::valid_audio_timeline(state));
   auto bad=state;++bad.output_frames;
   assert(!Core::valid_audio_timeline(bad));
   bad=state;bad.next=std::numeric_limits<double>::quiet_NaN();
   assert(!Core::valid_audio_timeline(bad));
   if(v2){
    eiii::fir_decimator::recall_state fir;
    assert(state.decimator.capture_recall_state(fir));fir.ratio=3;fir.phase=0;
    assert(bad.decimator.restore_recall_state(fir));bad.next=state.next;
    assert(!eiii::software_backend::valid_recall_state(bad));
   }
   backend.audio().clear();
  }
 }
}
