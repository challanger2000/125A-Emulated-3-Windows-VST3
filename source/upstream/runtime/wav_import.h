// Host-side WAV import data. No firmware, guest RAM or device implementation changes.
#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace eiii::wav {
constexpr size_t max_file_bytes = 64u * 1024u * 1024u;
constexpr size_t sample_ram_bytes = 0x7fd800; // OS reserves the top of the 8 MiB sample RAM.
struct Options {
    uint32_t root = 60, low = 0, high = 127, preset = 0;
    bool valid() const { return low <= root && root <= high && high <= 127 && preset < 100; }
};
struct Audio {
    uint32_t rate = 0, channels = 0;
    std::vector<float> pcm; // interleaved; immutable after decode, owned by emulator worker
    size_t frames() const { return channels ? pcm.size() / channels : 0; }
    double duration() const { return rate ? double(frames()) / rate : 0; }
    float at(double seconds, unsigned channel) const noexcept;
    float filtered_at(double seconds, unsigned channel, double adc_rate) const noexcept;
};
// Transactional: failure leaves the previous decoded asset unchanged.
bool decode(std::span<const uint8_t> bytes, Audio& output, std::string& error);
bool load(const std::string& path, Audio& output, std::string& error);
// Conservative bound, including stereo storage and per-sample header/rounding headroom.
bool fits(const Audio& audio, uint64_t available_bytes = sample_ram_bytes);
// A single-sample firmware capture has two equal-length planar channel blocks.
// Timing bounds come from observed ADC conversions; these are NOT generic
// permissions to trim source samples. Each source value must match exactly.
// Any ambiguous position, channel displacement or nonzero guard data fails.
struct CaptureWindow {
    size_t adc_before = 0, adc_after = 0, zero_guard = 0;
};
struct CaptureLayout { size_t channel_words = 0, pre = 0, post = 0; };
bool verify_payload(std::span<const uint16_t> recorded,
                    std::span<const int16_t> left,std::span<const int16_t> right,
                    CaptureWindow timing, CaptureLayout* measured = nullptr);
// Original OS 2.42 FORCE capture: one initial ADC read is discarded, then
// two leading and two trailing guard words bracket each planar channel.
// Source onset is scheduled only after recording begins. Exact event counts
// fix the source position, including for silence; no waveform search is used.
bool verify_forced_payload(std::span<const uint16_t> recorded,
                           std::span<const int16_t> left,std::span<const int16_t> right,
                           size_t adc_before,size_t adc_after,
                           CaptureLayout* measured = nullptr);
// Failure diagnostics only. Equal halves are a hypothesis, not header-derived
// channel bounds. Prefix/suffix counts never authorize trimming or acceptance.
struct CaptureDiagnostic {
    bool equal_halves = false;
    size_t assumed_channel_words = 0;
    std::string summary;
};
CaptureDiagnostic diagnose_payload(std::span<const uint16_t> recorded,
                                   std::span<const int16_t> left,
                                   std::span<const int16_t> right);
}
