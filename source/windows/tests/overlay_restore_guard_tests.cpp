#include "../vst3-port/overlay_restore_guard.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdio>
int main() {
 eiii::vst3::OverlayRestoreGuard g;
 assert(g.can_save() && g.can_replace_overlays());
 g.missing_disk_with_changes(false);
 assert(g.can_save());
 g.missing_disk_with_changes(true);
 assert(!g.can_save() && !g.can_replace_overlays());
 g.restore_warning(false);
 assert(!g.can_save());
 g.new_project();
 assert(g.can_save());
 g.restore_warning(true);
 assert(!g.can_save());
 g.new_project();
 assert(g.can_save() && g.can_replace_overlays());
 std::puts("PASS: B11 missing disk, restore rejection, sticky guard and new project reset");
}
