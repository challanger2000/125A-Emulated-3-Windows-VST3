// A04: production EIIIX/ESI conversion must reject corrupt FATs safely.
#include "../../upstream/runtime/eiii_bank_import.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <atomic>

namespace fs = std::filesystem;
void u16(std::vector<uint8_t>& b, size_t at, uint16_t x) {
    b.at(at) = uint8_t(x);
    b.at(at + 1) = uint8_t(x >> 8);
}
void u32(std::vector<uint8_t>& b, size_t at, uint32_t x) {
    for (unsigned i=0;i<4;++i) b.at(at+i)=uint8_t(x>>(i*8));
}
void make_disc(const fs::path& p, int pattern, bool truncated=false) {
    // EMU3 header, 1 directory entry at sector 9, FAT at sector 2.
    std::vector<uint8_t> b(truncated ? 8192 : 131072, 0);
    b[0]='E'; b[1]='M'; b[2]='U'; b[3]='3';
    u32(b,0x08,7); u32(b,0x10,9); u32(b,0x14,1);
    u32(b,0x18,2); u32(b,0x1c,1); u32(b,0x20,10);
    b[0x28]=1; // 128 sectors per cluster => 64 KiB
    const size_t dir=9*512;
    b[dir]='A'; b[dir+0x1a]=0x81;
    u16(b,dir+0x12,1);
    u16(b,dir+0x14,1);
    const size_t fat=2*512;
    if(pattern==1) u16(b,fat+2,1); // self-cycle
    else if(pattern==2) {
        u16(b,fat+2,2);
        u16(b,fat+4,1); // 1->2->1
    } else if(pattern==3) u16(b,fat+2,65534); // invalid FAT index
    else u16(b,fat+2,0x7fff); // good chain
    if(!truncated) {
        const size_t start=10*512;
        const char sig[]="EMULATOR THREE ";
        for(size_t i=0;i<15;++i)b[start+i]=uint8_t(sig[i]);
    }
    std::ofstream o(p,std::ios::binary | std::ios::trunc);
    o.write(reinterpret_cast<const char*>(b.data()),std::streamsize(b.size()));
    assert(o.good());
}
int main() {
    const auto id=std::chrono::steady_clock::now().time_since_epoch().count();
    const auto folder=fs::temp_directory_path()/("eiii_bank_a04_"+std::to_string(id));
    assert(fs::create_directory(folder));
    struct Cleanup { fs::path p; ~Cleanup(){std::error_code e;fs::remove_all(p,e);} } clean{folder};
    for(int pattern=1;pattern<=3;++pattern) {
        const auto input=folder/("corrupt"+std::to_string(pattern)+".iso");
        const auto output=folder/("out"+std::to_string(pattern)+".iso");
        make_disc(input,pattern);
        for(int n=0;n<3;++n) {
            std::string summary,error;
            assert(!eiii::bank_import::convert(input.string(),output.string(),summary,error));
            assert(!error.empty());
            assert(!fs::exists(output));
            assert(!fs::exists(output.string()+".part"));
        }
        // Prove the converter did not leak a source/output FILE*: all input
        // files can be moved after rejection (Windows locks open handles).
        const auto moved=folder/("moved"+std::to_string(pattern)+".iso");
        fs::rename(input,moved);
        fs::remove(moved);
    }
    // Physical truncation must also be rejected without a partial output.
    {
        const auto input=folder/"truncated.iso", output=folder/"truncated-output.iso";
        make_disc(input,0,true);
        std::string summary,error;
        assert(!eiii::bank_import::convert(input.string(),output.string(),summary,error));
        assert(!fs::exists(output.string()+".part"));
    }
    // Control case: valid one-cluster input must still be recognised and
    // converted; do not solve A04 by disabling all conversion.
    {
        const auto input=folder/"valid.iso", output=folder/"valid-output.iso";
        make_disc(input,0);
        std::string summary,error;
        assert(eiii::bank_import::convert(input.string(),output.string(),summary,error));
        assert(fs::exists(output) && fs::file_size(output)>512);
        assert(!fs::exists(output.string()+".part"));
    }
    std::puts("PASS: cyclic/out-of-bounds/truncated FAT refused, temp cleaned and valid conversion retained");
}
