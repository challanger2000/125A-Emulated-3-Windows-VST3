#pragma once
// B11: a failed disk delta restore must never be silently replaced during a host save.
// Main/plugin lifecycle thread only.
namespace eiii::vst3 {
class OverlayRestoreGuard {
 bool unsafe_ = false;
public:
 void new_project() noexcept { unsafe_ = false; }
 void missing_disk_with_changes(bool hasChanges) noexcept { if(hasChanges) unsafe_=true; }
 void restore_warning(bool hasWarning) noexcept { if(hasWarning) unsafe_=true; }
 bool can_save() const noexcept { return !unsafe_; }
 bool can_replace_overlays() const noexcept { return !unsafe_; }
};
}
