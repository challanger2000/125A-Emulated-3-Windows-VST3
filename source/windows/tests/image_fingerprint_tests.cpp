#include "../../upstream/standalone/shim/image_fingerprint.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
int main() {
    using eiii::shim::image_fingerprint;
    std::vector<unsigned char> a(131072,0x25);
    auto b=a;
    b[70000]^=0x7f; // real A03: beyond unchanged first 64 KiB
    image_fingerprint ha,hb,fragmented;
    ha.update(a.data(),a.size());
    hb.update(b.data(),b.size());
    for(size_t offset=0;offset<a.size();offset+=193) {
        auto n=std::min<size_t>(193,a.size()-offset);
        fragmented.update(a.data()+offset,n);
    }
    assert(ha.seen==a.size()&&hb.seen==b.size());
    assert(ha.prefix==hb.prefix); // demonstrate exact legacy defect
    assert(ha.full!=hb.full);     // full fingerprint closes this gap
    assert(ha.prefix==fragmented.prefix&&ha.full==fragmented.full);
    image_fingerprint shortImage;
    shortImage.update(a.data(),65535);
    assert(shortImage.prefix!=ha.prefix&&shortImage.full!=ha.full);
    std::puts("PASS: full-image HDC2 distinguishes equal-64KiB-prefix images and is stream-stable");
}
