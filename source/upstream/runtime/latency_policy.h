#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
namespace eiii {
// Shared between host-reported latency and actual engine setup.
// The audio ring capacity is fixed at 32768 frames in the current engine.
inline unsigned latency_for(double sample_rate, size_t max_block, size_t ring_capacity) noexcept {
 if(!(std::isfinite(sample_rate) && sample_rate>0)) return 0;
 const double scaled=std::round(1024.0*sample_rate/48000.0);
 const uint64_t floor=uint64_t(std::min(scaled, double(UINT32_MAX)));
 const uint64_t block=max_block> (UINT64_MAX-512)/2 ? UINT64_MAX : 2*uint64_t(max_block)+512;
 return unsigned(std::min<uint64_t>(std::max(floor,block), ring_capacity/2));
}
}
