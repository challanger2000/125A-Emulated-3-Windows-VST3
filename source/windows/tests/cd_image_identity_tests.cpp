// B02: read-only CD-ISO full-file identity and rejected mismatched media.
// Uses real shim CD-ROM device and two same-size synthetic MODE1 images.
// No proprietary firmware or actual sample libraries.
#include "emu.h"
#include "imagedev/cdromimg.h"
#include "cdrom.h"
#include "image_fingerprint.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <chrono>
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <vector>
#include <string>
#include <array>
#include <system_error>
int main() {
    namespace fs=std::filesystem;
    const auto base=fs::temp_directory_path() /
        ("125a-b02-cd-identity-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto a=fs::path(base.string()+"-a.iso");
    const auto b=fs::path(base.string()+"-b.iso");
    const auto truncated=fs::path(base.string()+"-partial.iso");
    const auto write=[&](fs::path const &path,std::vector<unsigned char> const& data) {
        std::ofstream stream(path,std::ios::binary|std::ios::trunc);
        assert(bool(stream));
        stream.write(reinterpret_cast<char const*>(data.data()),
                     static_cast<std::streamsize>(data.size()));
        assert(bool(stream));
    };
    std::vector<unsigned char> first(131072,0x4e),second=first;
    first[0]='E';first[1]='I';first[2]='I';first[3]='I';
    second=first;
    second[70000]^=0xa5; // *same size and first 64 KiB*, different ISO
    write(a,first);write(b,second);
    write(truncated,std::vector<unsigned char>(2050,0x22));
    {
        machine_config config;
        cdrom_image_device cd(config,"image",nullptr);
        assert(!cd.exists() && cd.length()==0 && cd.fingerprint()==0);
        assert(cd.load(a.string()));
        assert(cd.exists() && cd.length()==first.size());
        assert(cd.get_track_start(0xaa)==first.size()/2048);
        const auto fingerprintA=cd.fingerprint();
        assert(fingerprintA!=0);
        eiii::shim::image_fingerprint reference;
        reference.update(first.data(),first.size());
        assert(fingerprintA==reference.full);
        // Hashing the file must not leave its FILE* at EOF or corrupt reads.
        std::array<unsigned char,2048> sector{};
        assert(cd.read_data(0,sector.data(),cdrom_file::CD_TRACK_MODE1));
        assert(sector[0]=='E' && sector[3]=='I');
        const auto seqA=cd.sequence_counter();
        assert(cd.load(b.string()));
        assert(cd.sequence_counter()!=seqA);
        assert(cd.length()==first.size());
        const auto fingerprintB=cd.fingerprint();
        assert(fingerprintB!=fingerprintA);
        assert(cd.read_data(70000/2048,sector.data(),cdrom_file::CD_TRACK_MODE1));
        assert(sector[70000%2048]==second[70000]);
        assert(!cd.load(truncated.string()));
        assert(!cd.exists() && cd.length()==0 && cd.fingerprint()==0);
        assert(cd.load(a.string()));
        assert(cd.fingerprint()==fingerprintA); // repeatable, no stale state
        cd.unload();
        assert(!cd.exists() && cd.get_track_start(0xaa)==0);
        assert(cd.length()==0 && cd.fingerprint()==0);
    }
    std::error_code ec;
    fs::remove(a,ec);fs::remove(b,ec);fs::remove(truncated,ec);
    std::puts("PASS B02: CD ISO full-file fingerprint rejects same-size/header media, survives read/load/eject and invalid sizes");
}
