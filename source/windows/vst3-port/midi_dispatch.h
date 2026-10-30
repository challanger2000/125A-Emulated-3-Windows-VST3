#pragma once
// A08/A09: common MIDI forwarding for native VST3 process() and regressions.
// Zero allocation, bounded 16-channel loops, no locks on the audio thread.
#include "midi_routing.h"
#include "note_off_recovery.h"
#include <cstdint>
#include <utility>

namespace eiii::vst3 {
template<class Send>
void forward_controller(MidiRouting& route, NoteOffRecovery& recovery,
                        const uint8_t input[3], Send&& send) noexcept {
    const uint8_t source=input[0] & 15;
    const uint8_t command=input[0] & 0xf0;
    const bool sustain=(command==0xb0 && input[1]==64);
    const bool release=sustain && input[2]<64;
    const uint16_t targets=release ? route.sustain_off_destinations(source)
                                    : route.channel_destinations(source);
    const unsigned length=command==0xd0 ? 2 : 3;
    for (unsigned ch=0;ch<16;++ch) {
        if (!(targets & (1u<<ch))) continue;
        const uint8_t output[3]={uint8_t(command|ch),input[1],input[2]};
        if (release) {
            // NoteOffRecovery retains the release when the engine queue is
            // saturated. If the retry buffer is full, a per-channel CC64=0
            // plus All-Sound-Off panic is scheduled after queued releases.
            recovery.deliver(output,[&](const uint8_t* msg) noexcept {
                return send(msg,3);
            });
        } else if (send(output,length) && sustain) {
            route.sustain_on_delivered(source,uint8_t(ch));
        }
    }
    if (release) route.sustain_off_queued(source);
}
template<class Send>
void forward_poly_pressure(const MidiRouting& route,
                           const uint8_t input[3], Send&& send) noexcept {
    const uint8_t source=input[0]&15;
    const uint16_t targets=route.poly_destinations(source,input[1]);
    for(unsigned ch=0;ch<16;++ch) {
        if (!(targets & (1u<<ch))) continue;
        const uint8_t output[3]={uint8_t(0xa0|ch),input[1],input[2]};
        send(output,3);
    }
}
} // namespace eiii::vst3
