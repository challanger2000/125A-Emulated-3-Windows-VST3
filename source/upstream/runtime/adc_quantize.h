#pragma once
#include <cmath>
#include <cstdint>
#include <limits>

namespace eiii::adc {
// Clamp before rounding to avoid undefined/out-of-range long conversions.
// Non-finite audio is treated as silence rather than contaminating the ADC.
inline int16_t quantize(double sample, double gain) noexcept {
 if (!std::isfinite(sample) || !std::isfinite(gain)) return 0;
 const double v=sample*gain*32767.0;
 if (std::isnan(v)) return 0;
 if (v >= 32767.0) return 32767;
 if (v <= -32768.0) return -32768;
 return static_cast<int16_t>(std::lround(v));
}
}
