#pragma once
#include <cstdint>

// Sector validity for raw 512-byte EIII disk images. Promote before arithmetic:
// UINT32_MAX + 1 must never wrap around to sector zero.
namespace eiii::shim {
inline constexpr bool valid_sector(std::uint32_t lba, std::uint64_t image_bytes) noexcept {
    return (std::uint64_t(lba) + 1u) * 512u <= image_bytes;
}
}
