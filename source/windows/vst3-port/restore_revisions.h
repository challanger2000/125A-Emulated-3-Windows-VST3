#pragma once
// Request epochs distinguish current project state from stale worker completions.
// Manual MIDI edits and worker route commits are ordered by one short lock;
// the audio callback only observes the atomic MidiRouting target and never locks.
#include <cstdint>
#include <mutex>
#include <utility>

namespace eiii::vst3 {
class RestoreRevisions {
    mutable std::mutex mutex_;
    uint64_t generation_ = 0;
    uint64_t manual_route_revision_ = 0;
public:
    uint64_t new_project() noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        return ++generation_;
    }
    bool is_current(uint64_t generation) const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        return generation_ == generation;
    }
    uint64_t route_revision() const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        return manual_route_revision_;
    }
    template <class Apply>
    void manual_route_chosen(Apply&& apply) noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        ++manual_route_revision_;
        std::forward<Apply>(apply)();
    }
    // Called only after the serialized engine owner has committed a machine.
    // A newer queued UI request cannot cancel that already-live predecessor.
    // The engine generation check decides whether a machine may commit; only
    // a later deliberate manual route may override its accepted route here.
    template <class Apply>
    bool commit_owner_route(uint64_t route_revision, Apply&& apply) noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        if (manual_route_revision_ != route_revision) return false;
        std::forward<Apply>(apply)();
        return true;
    }
    template <class Apply>
    bool commit_route_if_current(uint64_t generation, uint64_t route_revision, Apply&& apply) noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        if (generation_ != generation || manual_route_revision_ != route_revision) return false;
        std::forward<Apply>(apply)();
        return true;
    }
};
} // namespace eiii::vst3
