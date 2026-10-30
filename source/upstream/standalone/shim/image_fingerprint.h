#pragma once
// A03: versioned image fingerprints. EIIIHDC1 historical compatibility used
// only the first 64 KiB, which is retained as a LEGACY, unverified indicator.
// EIIIHDC2 commits every byte; no partial-prefix aliasing can pass unnoticed
// merely because two disk images have the same system header and length.
#include <cstddef>
#include <cstdint>
namespace eiii::shim {
struct image_fingerprint {
    static constexpr std::uint64_t basis = UINT64_C(1469598103934665603);
    static constexpr std::uint64_t prime = UINT64_C(1099511628211);
    std::uint64_t prefix = basis;
    std::uint64_t full = basis;
    std::uint64_t seen = 0;
    void update(const void* ptr, std::size_t count) noexcept {
        const auto* b=static_cast<const unsigned char*>(ptr);
        for (std::size_t i=0; i<count; ++i) {
            const auto c=std::uint64_t(b[i]);
            full=(full ^ c)*prime;
            if (seen < 65536) prefix=(prefix ^ c)*prime;
            ++seen;
        }
    }
};
}
