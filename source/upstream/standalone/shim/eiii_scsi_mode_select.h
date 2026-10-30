#pragma once
// A05: bounds-checked SCSI MODE SELECT(6) parameters.
// Independent of MAME so malformed guest-supplied data can be fuzzed without
// running the original EIII firmware. The device consumes only valid fields.
#include <cstddef>
#include <cstdint>

namespace eiii::scsi {
struct ModeSelect6 {
    bool change_block_size = false;
    uint32_t block_size = 0;
    bool audio_page = false;
    bool sotc = false;
    float left_gain = 0.0f;
    float right_gain = 0.0f;
};

inline bool parse_mode_select6(const uint8_t* bytes, size_t size, ModeSelect6& result) noexcept {
    result = {};
    if (!bytes || size < 4 || size > 255) return false;
    const size_t descriptors = bytes[3];
    const size_t page = 4 + descriptors;
    if (page > size) return false;
    if (descriptors == 8) {
        // Standard 8-byte descriptor: 24-bit requested block length.
        const uint32_t block = (uint32_t(bytes[9]) << 16) |
                               (uint32_t(bytes[10]) << 8) | uint32_t(bytes[11]);
        if (!block || 2048u % block) return false;
        result.change_block_size = true;
        result.block_size = block;
    }
    if (page == size) return true; // header/descriptor only: no mode page
    if (size - page < 2) return false;
    const size_t payload = bytes[page + 1];
    if (payload > size - page - 2) return false;
    if ((bytes[page] & 0x3f) == 0x0e) {
        // Audio Control page uses bytes through page+15.
        if (payload < 14) return false;
        result.audio_page = true;
        result.sotc = (bytes[page+2] & 2) != 0;
        result.left_gain = bytes[page+9] / 255.0f;
        result.right_gain = bytes[page+11] / 255.0f;
    }
    return true;
}
} // namespace eiii::scsi
