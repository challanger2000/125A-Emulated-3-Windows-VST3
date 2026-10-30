#pragma once
// A10: ownership is the component/controller (non-audio) callback thread.
// No project state is mutated by observing the revision. Zero means untouched.
#include <cstdint>

namespace eiii::vst3 {
class ProjectDirtyRevision {
public:
    uint64_t changed() noexcept {
        // Saturation prevents a theoretical wraparound from masquerading as
        // an earlier revision. Normal projects will never approach this.
        if (revision_ != UINT64_MAX) ++revision_;
        return revision_;
    }
    uint64_t current() const noexcept { return revision_; }
private:
    uint64_t revision_ = 0;
};

// Controller-side de-duplication: only mark an unseen accepted change when
// the host handler is connected and has accepted the notification.
class DirtyDelivery {
public:
    template<class Send>
    bool observe(uint64_t revision, Send&& send) {
        if (revision == 0 || revision <= last_delivered_) return false;
        if (!send()) return false; // host unavailable: retry next status poll
        last_delivered_ = revision;
        return true;
    }
    uint64_t last_delivered() const noexcept { return last_delivered_; }
private:
    uint64_t last_delivered_ = 0;
};
}
