#include "../vst3-port/midi_dispatch.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

struct Packet {
    std::array<uint8_t,3> data{};
    unsigned length=0;
};
static bool has(const std::vector<Packet>& sent, unsigned status, unsigned data1,
                unsigned length) {
    for (const auto& p:sent)
        if(p.data[0]==status && p.data[1]==data1 && p.length==length) return true;
    return false;
}
int main() {
    using namespace eiii::vst3;
    MidiRouting route;
    NoteOffRecovery recovery;
    std::vector<Packet> sent;
    const auto send=[&](const uint8_t* bytes,unsigned n) noexcept {
        sent.push_back({{bytes[0],bytes[1],n==3?bytes[2]:uint8_t(0)},n});
        return true;
    };
    // Host passthrough, no active keys. Channel pressure is TWO bytes.
    {
        uint8_t cc[3]={0xb3,1,72};
        forward_controller(route,recovery,cc,send);
        assert(sent.size()==1 && has(sent,0xb3,1,3));
        sent.clear();
        uint8_t after[3]={0xd3,87,0};
        forward_controller(route,recovery,after,send);
        assert(sent.size()==1 && has(sent,0xd3,87,2));
        sent.clear();
    }
    // A08: six musical events all remap to target 14, not only NoteOns.
    route.set_target(14);
    assert(route.note_on(0,60)==13);
    {
        const uint8_t cc[3]={0xb0,1,73};
        forward_controller(route,recovery,cc,send);
        const uint8_t bend[3]={0xe0,3,64};
        forward_controller(route,recovery,bend,send);
        const uint8_t after[3]={0xd0,65,0};
        forward_controller(route,recovery,after,send);
        const uint8_t poly[3]={0xa0,60,92};
        forward_poly_pressure(route,poly,send);
        assert(sent.size()==4);
        assert(has(sent,0xbd,1,3));
        assert(has(sent,0xed,3,3));
        assert(has(sent,0xdd,65,2));
        assert(has(sent,0xad,60,3));
        sent.clear();
        // Poly-pressure on an unrelated pitch must use the current target
        // only; do not echo pressure onto another held voice.
        const uint8_t unrelated[3]={0xa0,65,99};
        forward_poly_pressure(route,unrelated,send);
        assert(sent.size()==1 && has(sent,0xad,65,3));sent.clear();
    }
    // Mixed destinations after a GUI routing change. CC applies to both
    // current route and old held notes; poly-pressure only to its pitch.
    route.set_target(5);
    assert(route.note_on(0,61)==4);
    const uint8_t wheel[3]={0xb0,1,111};
    forward_controller(route,recovery,wheel,send);
    assert(sent.size()==2 && has(sent,0xbd,1,3) && has(sent,0xb4,1,3));
    sent.clear();
    const uint8_t heldPressure[3]={0xa0,60,88};
    forward_poly_pressure(route,heldPressure,send);
    assert(sent.size()==1 && has(sent,0xad,60,3));
    sent.clear();
    const uint8_t overlap[3]={0xa0,61,88};
    forward_poly_pressure(route,overlap,send);
    assert(sent.size()==1 && has(sent,0xa4,61,3));
    sent.clear();
    // A09: even after the key is released, sustain-off must reach the
    // original pedal-on destination when routing is subsequently changed.
    route.reset();
    route.set_target(5);
    assert(route.note_on(0,60)==4);
    const uint8_t pedalOn[3]={0xb0,64,127};
    forward_controller(route,recovery,pedalOn,send);
    assert(has(sent,0xb4,64,3));
    assert(route.note_off(0,60)==4);
    route.set_target(14);
    sent.clear();
    // The key is physically up but still sounding from the pedal on old
    // target CH5. Bend/modulation must reach both old and new destinations;
    // poly pressure is specific to the sustained pitch and its old target.
    const uint8_t bent[3]={0xe0,1,75};
    forward_controller(route,recovery,bent,send);
    assert(sent.size()==2 && has(sent,0xe4,1,3) && has(sent,0xed,1,3));
    sent.clear();
    const uint8_t sustainedPressure[3]={0xa0,60,109};
    forward_poly_pressure(route,sustainedPressure,send);
    assert(sent.size()==1 && has(sent,0xa4,60,3));
    sent.clear();
    const uint8_t otherPitch[3]={0xa0,61,109};
    forward_poly_pressure(route,otherPitch,send);
    assert(sent.size()==1 && has(sent,0xad,61,3));
    sent.clear();
    const uint8_t pedalOff[3]={0xb0,64,0};
    forward_controller(route,recovery,pedalOff,send);
    assert(sent.size()==2 && has(sent,0xb4,64,3) && has(sent,0xbd,64,3));
    assert(route.sustain_off_destinations(0)==(1u<<13));
    sent.clear();
    forward_poly_pressure(route,sustainedPressure,send);
    assert(sent.size()==1 && has(sent,0xad,60,3)); // old pedal voice released
    sent.clear();
    forward_controller(route,recovery,pedalOff,send);
    assert(sent.size()==1 && has(sent,0xbd,64,3));sent.clear();

    // A09 saturation: the main MIDI queue refuses new packets and the
    // fixed retry queue is entirely full. The pedal release must trigger
    // the existing per-channel panic: CC64 OFF then CC120, never lost.
    route.reset();recovery.reset();route.set_target(14);
    assert(route.note_on(0,64)==13);
    forward_controller(route,recovery,pedalOn,send);
    assert(route.note_off(0,64)==13);
    sent.clear();
    const uint8_t oldOff[3]={0x8d,64,0};
    for(size_t i=0;i<recovery.capacity;++i)assert(recovery.defer(oldOff));
    int rejected=0;
    forward_controller(route,recovery,pedalOff,[&](const uint8_t*,unsigned) noexcept {
        ++rejected;return false;
    });
    assert(rejected==0 && recovery.pending()==recovery.capacity &&
           recovery.panic_stage(13)==1);
    // Recovery is serialized: try blocked first, then release all queued
    // NoteOffs, then the emergency sustain-off and all-sound-off.
    int blocked=0;
    recovery.retry([&](const uint8_t*) noexcept {++blocked;return false;});
    assert(blocked==1 && recovery.pending()==recovery.capacity);
    std::vector<std::array<uint8_t,3>> drained;
    for(size_t pass=0;pass<=recovery.capacity/recovery.max_retry_per_block+2;++pass) {
        recovery.retry([&](const uint8_t* p) noexcept {
            drained.push_back({p[0],p[1],p[2]}); return true;
        });
    }
    assert(recovery.pending()==0 && recovery.panic_stage(13)==0);
    assert(drained.size()==recovery.capacity+2);
    for(size_t i=0;i<recovery.capacity;++i)assert(drained[i]==(std::array<uint8_t,3>{0x8d,64,0}));
    assert((drained[recovery.capacity]==std::array<uint8_t,3>{0xbd,64,0}));
    assert((drained[recovery.capacity+1]==std::array<uint8_t,3>{0xbd,120,0}));

    // When retry has space, the pedal release is queued, not escalated.
    route.reset();recovery.reset();route.set_target(14);
    recovery.defer(oldOff);
    int immediate=0;
    forward_controller(route,recovery,pedalOff,[&](const uint8_t*,unsigned) noexcept {
        ++immediate;return true;
    });
    assert(immediate==0 && recovery.pending()==2 && recovery.panic_stage(13)==0);
    drained.clear();
    recovery.retry([&](const uint8_t* m) noexcept {
        drained.push_back({m[0],m[1],m[2]});return true;
    });
    assert(drained.size()==2 &&
           (drained[0]==std::array<uint8_t,3>{0x8d,64,0}) &&
           (drained[1]==std::array<uint8_t,3>{0xbd,64,0}));
    std::puts("PASS: A08 controller/poly-pressure routing and A09 queue-full sustain release/panic");
}
