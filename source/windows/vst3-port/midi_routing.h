// MIDI channel routing is host-side only; the EIII's own MIDI firmware is unchanged.
// The audio callback owns active note bookkeeping. The requested channel is atomic
// because the controller can change it from the UI thread.
#pragma once
#include <array>
#include <atomic>
#include <cstdint>

namespace eiii::vst3 {
class MidiRouting
{
public:
    static constexpr uint8_t kRejected = 255;
    static constexpr unsigned kMaxOverlap = 16;
    // 0 = preserve the host MIDI channel. 1..16 = route notes to the chosen EIII channel.
    void set_target(unsigned channel) noexcept
    {
        if (channel <= 16) target_.store(uint8_t(channel), std::memory_order_relaxed);
    }
    unsigned target() const noexcept { return target_.load(std::memory_order_relaxed); }

    // On the audio thread, remember the actual destination for each input note.
    // A routing change cannot strand a sounding note on its old channel.
    uint8_t note_on(uint8_t channel, uint8_t pitch) noexcept
    {
        const uint8_t selected = uint8_t(target());
        const uint8_t output = selected ? uint8_t(selected - 1) : uint8_t(channel & 15);
        auto &notes = active_[channel & 15][pitch & 127];
        if (notes.count == kMaxOverlap) return kRejected; // Never send an untracked NoteOn.
        notes.dest[(notes.head + notes.count) % kMaxOverlap] = uint8_t(output + 1);
        ++notes.count;
        return output;
    }
    uint8_t note_off(uint8_t channel, uint8_t pitch) noexcept
    {
        auto &notes = active_[channel & 15][pitch & 127];
        const uint8_t selected = uint8_t(target());
        if (notes.count) {
            const uint8_t output = uint8_t(notes.dest[notes.head] - 1);
            // A released key may still sound under an accepted CC64 pedal.
            // Retain the exact pitch/destination until the pedal is released.
            if (sustain_destinations_[channel & 15] & (1u << output))
                sustained_notes_[channel & 15][pitch & 127] |= uint16_t(1u << output);
            notes.head = uint8_t((notes.head + 1) % kMaxOverlap);
            --notes.count;
            return output;
        }
        return uint8_t(selected ? selected - 1 : (channel & 15));
    }
    // A rejected NoteOn must not leave a phantom tracked voice.
    void cancel_note_on(uint8_t channel, uint8_t pitch) noexcept
    {
        auto &notes = active_[channel & 15][pitch & 127];
        if (notes.count) --notes.count;
    }
    // A rejected NoteOff must retain its original destination for retry.
    uint8_t peek_note_off(uint8_t channel, uint8_t pitch) const noexcept
    {
        const auto &notes = active_[channel & 15][pitch & 127];
        const uint8_t selected = uint8_t(target());
        return notes.count ? uint8_t(notes.dest[notes.head] - 1)
            : uint8_t(selected ? selected - 1 : (channel & 15));
    }
    // Channel-wide MIDI messages affect new notes at the selected target
    // AND notes held on destinations selected before a routing change.
    // Return a fixed 16-bit set of zero-based destination channels.
    uint16_t channel_destinations(uint8_t channel) const noexcept
    {
        const unsigned source = channel & 15;
        uint16_t mask = uint16_t(fallback_destination(source) | sustain_destinations_[source]);
        for (const auto& notes : active_[source]) {
            for (unsigned i=0; i<notes.count; ++i)
                mask |= uint16_t(1u << (notes.dest[(notes.head+i)%kMaxOverlap]-1));
        }
        return mask;
    }
    // Poly pressure is per NOTE: do not send it to unrelated held pitches.
    uint16_t poly_destinations(uint8_t channel, uint8_t pitch) const noexcept
    {
        const unsigned source=channel & 15;
        const auto& notes=active_[source][pitch & 127];
        uint16_t mask=sustained_notes_[source][pitch & 127];
        for (unsigned i=0;i<notes.count;++i)
            mask |= uint16_t(1u << (notes.dest[(notes.head+i)%kMaxOverlap]-1));
        return mask ? mask : fallback_destination(source);
    }
    // Remember each destination that actually accepted sustain ON. A routing
    // change or NoteOff must not strand the pedal on its old MIDI channel.
    void sustain_on_delivered(uint8_t channel, uint8_t destination) noexcept
    {
        sustain_destinations_[channel & 15] |= uint16_t(1u << (destination & 15));
    }
    uint16_t sustain_off_destinations(uint8_t channel) const noexcept
    {
        return uint16_t(channel_destinations(channel) | sustain_destinations_[channel & 15]);
    }
    // Only call after release is accepted by the emulator or by the bounded
    // NoteOffRecovery fallback (which guarantees retry/panic on saturation).
    void sustain_off_queued(uint8_t channel) noexcept
    {
        const unsigned source=channel & 15;
        sustain_destinations_[source] = 0;
        sustained_notes_[source].fill(0);
    }
    void reset() noexcept
    {
        for (auto &ch : active_) for (auto &note : ch) note = {};
        sustain_destinations_.fill(0);
        for (auto &notes : sustained_notes_) notes.fill(0);
    }
private:
    std::atomic<uint8_t> target_{0};
    // Only read and written inside process(); never accessed by the GUI thread.
    struct NoteStack {
        std::array<uint8_t,kMaxOverlap> dest{};
        uint8_t head = 0, count = 0;
    };
    std::array<std::array<NoteStack,128>,16> active_{};
    // Audio-thread-owned masks; no locks, allocations or GUI-thread access.
    std::array<uint16_t,16> sustain_destinations_{};
    // Per-pitch destinations for keys released while a sustain pedal held.
    std::array<std::array<uint16_t,128>,16> sustained_notes_{};
    uint16_t fallback_destination(unsigned source) const noexcept
    {
        const unsigned selected=target();
        return uint16_t(1u << (selected ? selected-1 : (source&15)));
    }
};
} // namespace eiii::vst3
