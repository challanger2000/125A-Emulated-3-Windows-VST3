#include "../vst3-port/midi_routing.h"
#ifdef NDEBUG
#undef NDEBUG // Keep assertions enabled in Release/CI tests.
#endif
#include <cassert>
#include <cstdio>
#include <atomic>
#include <cstdint>
#include <thread>

int main()
{
    eiii::vst3::MidiRouting route;
    assert(route.target() == 0);
    assert(route.note_on(0, 60) == 0);
    assert(route.note_off(0, 60) == 0);

    route.set_target(14);
    assert(route.target() == 14);
    assert(route.note_on(0, 60) == 13);
    assert(route.note_off(0, 60) == 13);
    assert(route.note_on(13, 72) == 13);
    assert(route.note_off(13, 72) == 13);

    // Changing target while a key is pressed must not strand its NoteOff.
    route.set_target(5);
    assert(route.note_on(0, 64) == 4);
    route.set_target(9);
    assert(route.note_off(0, 64) == 4);
    assert(route.note_on(0, 64) == 8);
    assert(route.note_off(0, 64) == 8);

    // Different source channels can hold the same pitch simultaneously.
    route.set_target(14);
    assert(route.note_on(1, 48) == 13);
    route.set_target(3);
    assert(route.note_on(2, 48) == 2);
    assert(route.note_off(1, 48) == 13);
    assert(route.note_off(2, 48) == 2);

    // Velocity-zero NoteOn uses note_off() through the VST3 adapter.
    route.set_target(0);
    assert(route.note_on(15, 127) == 15);
    route.set_target(14);
    assert(route.note_off(15, 127) == 15);
    route.set_target(99); // malformed routing requests are ignored
    assert(route.target() == 14);
    route.reset();
    assert(route.note_off(15, 127) == 13);
    // Fallback must select the target exactly once during a concurrent GUI change.
    route.set_target(0);
    assert(route.note_off(3, 33) == 3);
    route.set_target(16);
    assert(route.note_off(3, 33) == 15);
    route.set_target(1);
    assert(route.note_off(3, 33) == 0);

    // Overlapping identical notes must release in the order they were routed.
    route.reset();
    route.set_target(1);
    assert(route.note_on(0, 60) == 0);
    route.set_target(14);
    assert(route.note_on(0, 60) == 13);
    assert(route.note_off(0, 60) == 0);
    assert(route.note_off(0, 60) == 13);
    route.reset();
    for (unsigned i=0; i<route.kMaxOverlap; ++i)
        assert(route.note_on(1, 42) == 13);
    assert(route.note_on(1, 42) == route.kRejected);
    for (unsigned i=0; i<route.kMaxOverlap; ++i)
        assert(route.note_off(1, 42) == 13);

    // Rejected events must not corrupt note ownership.
    route.reset();
    route.set_target(1);
    assert(route.note_on(0, 64) == 0);
    route.set_target(14);
    assert(route.peek_note_off(0, 64) == 0);
    assert(route.peek_note_off(0, 64) == 0);
    assert(route.note_off(0, 64) == 0);
    assert(route.note_on(0, 64) == 13);
    route.cancel_note_on(0, 64);
    assert(route.peek_note_off(0, 64) == 13);


    // R07: GUI routing updates race against audio-thread fallback NoteOff.
    // The old double atomic read could observe target=16, then target=0,
    // causing unsigned underflow to an invalid destination (255).
    {
        route.reset();
        route.set_target(0);
        std::atomic<bool> begin{false};
        std::thread gui([&] {
            while (!begin.load(std::memory_order_acquire)) std::this_thread::yield();
            for (unsigned i=0; i<500000; ++i) {
                route.set_target((i & 1u) ? 0u : 16u);
                if ((i & 255u)==0u) std::this_thread::yield();
            }
        });
        begin.store(true,std::memory_order_release);
        for (unsigned i=0; i<500000; ++i) {
            const auto off=route.note_off(3,33);
            const auto preview=route.peek_note_off(3,33);
            const auto mask=route.channel_destinations(3);
            const auto poly=route.poly_destinations(3,33);
            assert((off==3 || off==15) && (preview==3 || preview==15));
            assert((mask==uint16_t(1u<<3) || mask==uint16_t(1u<<15)));
            assert((poly==uint16_t(1u<<3) || poly==uint16_t(1u<<15)));
            if ((i & 255u)==0u) std::this_thread::yield();
        }
        gui.join();
        route.set_target(0);
        assert(route.note_off(3,33)==3);
        route.set_target(16);
        assert(route.note_off(3,33)==15);
    }

    std::puts("MIDI routing: channel remap, state and held-note migration PASS");
}
