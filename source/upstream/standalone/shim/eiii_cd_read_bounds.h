#pragma once
// A17: CD READ(6)/(10) may not cross the known MODE1 disc capacity.
// All computations are unsigned 64-bit and avoid an LBA+count overflow.
#include <cstdint>
namespace eiii::scsi {
inline bool cd_read_range_valid(uint64_t first_block, uint64_t blocks,
                                uint64_t disc_frames, uint32_t block_bytes) noexcept {
    if (!block_bytes || 2048u % block_bytes) return false;
    const uint64_t available_blocks = disc_frames * (2048u / block_bytes);
    return first_block <= available_blocks && blocks <= available_blocks - first_block;
}
}
