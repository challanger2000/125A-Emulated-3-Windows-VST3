#include "../vst3-port/project_activation.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdio>
int main() {
 eiii::vst3::ProjectActivation activation;
 assert(!activation.pending());
 activation.restored(true);
 assert(activation.pending() && !activation.capture_old_engine());
 // Host deactivation must not overwrite newly restored media with old media.
 assert(!activation.capture_old_engine());
 activation.activated();
 assert(!activation.pending() && activation.capture_old_engine());
 activation.restored(false);
 assert(!activation.pending() && activation.capture_old_engine());
 activation.restored(true);
 activation.restored(true);
 assert(activation.pending());
 activation.activated();
 assert(!activation.pending());
 std::puts("PASS: B04 stage, preserve through deactivation, reactivate, inactive restore");
}
