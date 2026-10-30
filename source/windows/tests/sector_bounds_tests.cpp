#include "../../upstream/standalone/shim/sector_bounds.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdint>
#include <cstdio>
int main() {
    using eiii::shim::valid_sector;
    assert(!valid_sector(0, 0));
    assert(!valid_sector(0, 511));
    assert(valid_sector(0, 512));
    assert(valid_sector(7, 4096));
    assert(!valid_sector(8, 4096));
    assert(!valid_sector(UINT32_MAX, 4096));
    assert(!valid_sector(UINT32_MAX, 0));
    assert(!valid_sector(UINT32_MAX, 0xFFFFFFFFull));
    assert(valid_sector(UINT32_MAX, (UINT64_C(1) << 41)));
    std::puts("EIII disk sector bounds PASS");
}
