#pragma once
// Host-controlled project restore staging. Never replace an incoming state
// with outgoing emulator media when the host restores while the VST3 is active.
namespace eiii::vst3 {
class ProjectActivation {
 bool pending_ = false;
public:
 void restored(bool engineActive) noexcept { pending_ = engineActive; }
 bool pending() const noexcept { return pending_; }
 bool capture_old_engine() const noexcept { return !pending_; }
 void activated() noexcept { pending_ = false; }
};
}
