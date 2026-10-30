#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace eiii::vst3 {
// Single audio-thread owner, fixed-memory retry queue. The callable returns true
// only if the underlying emulator queue accepted all three MIDI bytes.
class NoteOffRecovery {
public:
 static constexpr size_t capacity=4096;
 static constexpr size_t max_retry_per_block=128;
 template<class Send> void retry(Send&& send) noexcept {
  size_t budget=max_retry_per_block;
  while(count_ && budget) {
   const auto &msg=pending_[head_];
   if(!send(msg.data())) break;
   head_=(head_+1)%capacity;
   --count_;
   --budget;
  }
  if(count_) return; // preserve ordering: never send panic ahead of pending releases
  for(unsigned ch=0;ch<16;++ch) {
   while(panic_[ch] && budget) {
    const uint8_t stage=panic_[ch];
    const uint8_t cmd[3]={uint8_t(0xb0|ch),uint8_t(stage==1?64:120),0};
    if(!send(cmd)) return;
    panic_[ch]=stage==1?2:0;
    --budget;
   }
  }
 }
 template<class Send> bool deliver(const uint8_t msg[3], Send&& send) noexcept {
  return (count_==0 && send(msg)) || defer(msg);
 }
 bool defer(const uint8_t msg[3]) noexcept {
  if(count_==capacity) {
   panic_[msg[0]&15]=1;
   return true;
  }
  auto &dst=pending_[(head_+count_)%capacity];
  for(unsigned i=0;i<3;++i)dst[i]=msg[i];
  ++count_;
  return true;
 }
 void reset() noexcept { head_=count_=0; panic_.fill(0); }
 size_t pending() const noexcept {return count_;}
 uint8_t panic_stage(unsigned ch) const noexcept {return panic_[ch&15];}
private:
 std::array<std::array<uint8_t,3>,capacity> pending_{};
 size_t head_=0,count_=0;
 std::array<uint8_t,16> panic_{};
};
}
