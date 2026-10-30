#include "../../upstream/standalone/shim/eiii_cd_read_bounds.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <limits>

int main() {
    using eiii::scsi::cd_read_range_valid;
    // A one-frame test ISO has exactly one 2048-byte data sector.
    assert(cd_read_range_valid(0,1,1,2048));
    assert(!cd_read_range_valid(1,1,1,2048)); // first invalid LBA
    assert(cd_read_range_valid(1,0,1,2048)); // legal zero-length READ10
    assert(!cd_read_range_valid(2,0,1,2048));
    assert(cd_read_range_valid(0,4,1,512));
    assert(cd_read_range_valid(3,1,1,512));
    assert(!cd_read_range_valid(3,2,1,512));
    assert(!cd_read_range_valid(0,1,0,2048));
    assert(cd_read_range_valid(0,0,0,2048));
    assert(!cd_read_range_valid(0,1,1,0));
    assert(!cd_read_range_valid(0,1,1,768));
    // READ10 near UINT32_MAX must reject without wraparound.
    assert(!cd_read_range_valid(std::numeric_limits<uint32_t>::max(),65535,42,2048));
    assert(!cd_read_range_valid(std::numeric_limits<uint64_t>::max(),2,42,512));
    // Leading and trailing large-LBA boundaries.
    assert(cd_read_range_valid(999,1,1000,2048));
    assert(!cd_read_range_valid(999,2,1000,2048));
    assert(cd_read_range_valid(3999,1,1000,512));
    std::puts("PASS: A17 CD READ(6/10) bounds, last LBA, 512/2048 and overflow");
}
