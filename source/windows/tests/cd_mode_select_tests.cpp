#include "../../upstream/standalone/shim/eiii_scsi_mode_select.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
using eiii::scsi::ModeSelect6;
using eiii::scsi::parse_mode_select6;

int main() {
    std::array<uint8_t,256> d{};
    ModeSelect6 p{};
    assert(!parse_mode_select6(d.data(), 3, p));
    // Reproduce A05-2: transferred 4 bytes but descriptor count 255.
    d[3]=255;
    assert(!parse_mode_select6(d.data(), 4, p));
    d.fill(0);
    // Reproduce A05-1: 12 bytes, descriptor count 8, zero block length.
    d[3]=8;
    assert(!parse_mode_select6(d.data(), 12, p));
    // Valid 2048-byte and 512-byte blocks retain their previous behaviour.
    d[10]=8; // 0x000800 = 2048
    assert(parse_mode_select6(d.data(), 12, p));
    assert(p.change_block_size && p.block_size==2048 && !p.audio_page);
    d[10]=2; // 512
    assert(parse_mode_select6(d.data(), 12, p));
    assert(p.change_block_size && p.block_size==512);
    d[10]=3; // 768, not a factor of CD sector size
    assert(!parse_mode_select6(d.data(), 12, p));
    d[10]=2;
    assert(!parse_mode_select6(d.data(), 11, p)); // incomplete descriptor
    // Valid page 0x0E has payload length 14 and indexes through page+15.
    d[12]=0x0E; d[13]=14; d[14]=2;
    d[21]=255; d[23]=128;
    assert(parse_mode_select6(d.data(), 28, p));
    assert(p.audio_page && p.sotc && p.left_gain==1.0f);
    assert(std::abs(p.right_gain - 128.0f/255.0f) < 1e-6f);
    assert(!parse_mode_select6(d.data(), 27, p)); // short final page
    d[13]=13;
    assert(!parse_mode_select6(d.data(), 27, p)); // page too short for gain
    // Descriptorless MODE SELECT is also valid, with no block-size change.
    d.fill(0);
    assert(parse_mode_select6(d.data(), 4, p));
    assert(!p.change_block_size && !p.audio_page);
    // No speculative default read from untransferred page bytes.
    assert(!parse_mode_select6(d.data(), 5, p));
    std::puts("PASS: CD MODE SELECT(6) rejects zero/oversized descriptors and preserves valid sizes/pages");
}
