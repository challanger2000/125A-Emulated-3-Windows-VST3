#include "../vst3-port/project_dirty_revision.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdio>
int main(){
 using namespace eiii::vst3;
 ProjectDirtyRevision component;
 DirtyDelivery controller;
 int calls=0;
 auto report=[&](){++calls;return true;};
 assert(!controller.observe(component.current(),report)&&calls==0);
 // read/poll/rejected request: no call to changed(), still clean
 assert(component.current()==0);
 const auto first=component.changed();
 assert(controller.observe(first,report)&&calls==1);
 assert(!controller.observe(first,report)&&calls==1); // duplicate poll
 const auto second=component.changed();
 assert(!controller.observe(second,[&]{++calls;return false;})&&calls==2);
 assert(controller.last_delivered()==first);
 assert(controller.observe(second,report)&&calls==3); // retry
 assert(!controller.observe(first,report)&&calls==3); // stale/out-of-order
 const auto third=component.changed();
 const auto fourth=component.changed();
 assert(!controller.observe(third,[&](){++calls;return false;}));
 assert(controller.observe(fourth,report)); // coalesced missed revisions
 assert(controller.last_delivered()==fourth);
 std::puts("PASS A10: revision dedup, no-poll dirty, failed handler retry, stale suppression");
}
