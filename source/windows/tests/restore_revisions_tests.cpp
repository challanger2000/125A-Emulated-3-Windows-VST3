#include "../vst3-port/restore_revisions.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <atomic>
#include <cassert>
#include <cstdio>
#include <thread>

int main() {
    eiii::vst3::RestoreRevisions rev;
    std::atomic<unsigned> destination{0};
    const auto older = rev.new_project();
    const auto older_route = rev.route_revision();
    const auto newest = rev.new_project();
    const auto newest_route = rev.route_revision();

    assert(!rev.is_current(older));
    assert(!rev.commit_route_if_current(older, older_route, [&] { destination.store(3); }));
    assert(rev.is_current(newest));
    assert(rev.commit_route_if_current(newest, newest_route, [&] { destination.store(14); }));
    assert(destination.load() == 14);

    // A deliberate user selection after the request but before its callback
    // must persist; compare + write must be one indivisible operation.
    rev.manual_route_chosen([&] { destination.store(7); });
    assert(!rev.commit_route_if_current(newest, newest_route, [&] { destination.store(14); }));
    assert(destination.load() == 7);

    const auto third = rev.new_project();
    const auto third_route = rev.route_revision();
    assert(rev.commit_route_if_current(third, third_route, [&] { destination.store(11); }));
    assert(destination.load() == 11);

    // An engine-approved predecessor still owns the live machine while a
    // newer UI request is only queued. Its route must follow the live machine.
    const auto approved_route=rev.route_revision();
    rev.new_project();
    assert(rev.commit_owner_route(approved_route,[&]{destination.store(6);}));
    rev.manual_route_chosen([&]{destination.store(9);});
    assert(!rev.commit_owner_route(approved_route,[&]{destination.store(6);}));
    assert(destination.load()==9);

    // Exercise genuinely competing main/worker updates; whichever obtains
    // the short lock last defines the observable route.
    for (int i=0; i<300; ++i) {
        const auto generation = rev.new_project();
        const auto route = rev.route_revision();
        std::thread worker([&] {
            rev.commit_route_if_current(generation, route, [&] { destination.store(14); });
        });
        rev.manual_route_chosen([&] { destination.store(7); });
        worker.join();
        assert(destination.load() == 7);
    }
    std::puts("PASS: stale restore rejected and later manual MIDI route cannot be overwritten");
}
