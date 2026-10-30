// Audio input adapter for EIII's original ADC emulation.
// Converts the optional VST3 auxiliary input to a bounded, finite stereo
// float block without allocating or blocking in the audio callback.
// This file deliberately has no dependency on Steinberg SDK headers.
#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace eiii::sampling {
struct SampleInput
{
    unsigned channels = 0;
    bool is64 = false;
    uint64_t silence_flags = 0; // VST3: one silence bit per channel
    const float* const* data32 = nullptr;
    const double* const* data64 = nullptr;
};

// All output arrays must be preallocated with at least 'frames' entries.
// Missing channel 1: duplicate channel 0 (a mono source feeds stereo ADC).
// Missing input or flagged-silent channels produce actual zero-valued samples.
// Invalid NaN/Inf or out-of-float-range double values become zero.
inline void copy_sample_input(const SampleInput& source, float* left, float* right,
                              size_t frames) noexcept
{
    for(size_t i=0;i<frames;++i) {
        auto read_channel=[&](unsigned channel) noexcept -> float {
            if(source.channels==0) return 0.f;
            const unsigned selected=source.channels>channel ? channel : 0;
            if(source.silence_flags & (uint64_t(1)<<selected)) return 0.f;
            if(source.is64) {
                const double* samples=source.data64 ? source.data64[selected] : nullptr;
                if(!samples || !std::isfinite(samples[i])) return 0.f;
                const float value=static_cast<float>(samples[i]);
                return std::isfinite(value) ? value : 0.f;
            }
            const float* samples=source.data32 ? source.data32[selected] : nullptr;
            if(!samples) return 0.f;
            return std::isfinite(samples[i]) ? samples[i] : 0.f;
        };
        left[i]=read_channel(0);
        right[i]=read_channel(1);
    }
}
} // namespace eiii::sampling
