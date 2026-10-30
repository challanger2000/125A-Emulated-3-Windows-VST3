// 125A EIII Windows VST3: project state v5 (reads v1/v2/v3/v4), little-endian and bounds checked.
// The original EIII OS continues to own bank/sample RAM. The project stores
// media references and the changed sectors of READ-ONLY disk images, not ROMs.
#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <utility>

namespace eiii::vst3 {
struct ProjectState
{
    uint32_t midi_channel = 0; // 0 = original host channel, 1..16 = remap
    std::string rom_dir, hd_path, cd_path;
    std::string floppy_path; // v5: D0 read-only IMG/HFE, never embed floppy contents
    std::string cem_model = "v2"; // new project default; serialized v1/v2 choices are preserved
    std::array<uint32_t,4> extra_kind{}; // D4..D7, 0 none, 1 HDD, 2 CD
    std::array<std::string,4> extra_path{};
    std::array<std::string,5> hd_changes{}; // D1, D4..D7; original files untouched
    // Firmware-driven automatic recall from D1 when the instrument boots.
    int32_t autoload_bank = -1;   // -1 disabled, B00..B99
    int32_t autoload_preset = -1; // -1 bank's original selection, P00..P99
    std::vector<uint8_t> machine_snapshot; // v4: validated device-state envelope
};
constexpr uint32_t kStateMagic = 0x33494945u; // 'EII3' as previously released
constexpr uint32_t kStateVersion = 5;
constexpr size_t kMaxMachineSnapshot = 192u*1024u*1024u + 20u;
constexpr size_t kMaxPath = 4096, kMaxDiskChanges = 128u*1024u*1024u;
constexpr size_t kMaxStateBytes = 256u*1024u*1024u;
struct DecodedState {
    ProjectState value;
    bool midi_only_v1 = false;
    bool has_autoload_v3 = false;
};

inline bool valid_state(ProjectState const& s)
{
    if (s.midi_channel > 16 || (s.cem_model != "v1" && s.cem_model != "v2") ||
        s.machine_snapshot.size() > kMaxMachineSnapshot) return false;
    if (s.autoload_bank < -1 || s.autoload_bank > 99 ||
        s.autoload_preset < -1 || s.autoload_preset > 99 ||
        (s.autoload_bank < 0 && s.autoload_preset >= 0)) return false;
    auto valid_path = [](std::string const& path) {
        return path.size() <= kMaxPath && path.find('\0') == std::string::npos;
    };
    if (!valid_path(s.rom_dir) || !valid_path(s.hd_path) || !valid_path(s.cd_path) ||
        !valid_path(s.floppy_path)) return false;
    size_t total = 12 + 4*(4+2*kMaxPath);
    for (size_t i=0;i<4;++i) {
        if (s.extra_kind[i]>2 || !valid_path(s.extra_path[i]) ||
            (s.extra_kind[i]==0 && !s.extra_path[i].empty()) ||
            (s.extra_kind[i]==1 && s.extra_path[i].empty()))
            return false;
    }
    for (size_t i=0;i<5;++i) {
        if (s.hd_changes[i].size()>kMaxDiskChanges) return false;
        total += s.hd_changes[i].size();
        if (total>kMaxStateBytes) return false;
    }
    return total + s.machine_snapshot.size() + s.floppy_path.size() + 12 <= kMaxStateBytes;
}
inline void put32(std::vector<uint8_t>& out,uint32_t v)
{
    for(unsigned i=0;i<4;++i) out.push_back(uint8_t(v>>(i*8)));
}
inline bool get32(uint8_t const* data,size_t size,size_t& cursor,uint32_t& value)
{
    if (size-cursor<4) return false;
    value=0;
    for(unsigned i=0;i<4;++i) value|=uint32_t(data[cursor++])<<(i*8);
    return true;
}
inline void put_string(std::vector<uint8_t>& out,std::string const& value)
{
    put32(out,uint32_t(value.size()));
    out.insert(out.end(),value.begin(),value.end());
}
inline bool get_string(uint8_t const* data,size_t size,size_t& cursor,std::string& result,size_t maximum)
{
    uint32_t count=0;
    if(!get32(data,size,cursor,count) || count>maximum || count>size-cursor)
        return false;
    result.assign(reinterpret_cast<char const*>(data+cursor),size_t(count));
    cursor += count;
    return true;
}
inline bool encode_state(ProjectState const& state,std::vector<uint8_t>& out)
{
    if(!valid_state(state)) return false;
    std::vector<uint8_t> temp;
    temp.reserve(256);
    put32(temp,kStateMagic);
    put32(temp,kStateVersion);
    put32(temp,state.midi_channel);
    put_string(temp,state.rom_dir);
    put_string(temp,state.hd_path);
    put_string(temp,state.cd_path);
    put_string(temp,state.cem_model);
    for(size_t i=0;i<4;++i) {
        put32(temp,state.extra_kind[i]);
        put_string(temp,state.extra_path[i]);
    }
    for(auto const& change : state.hd_changes) put_string(temp,change);
    put32(temp,uint32_t(state.autoload_bank));
    put32(temp,uint32_t(state.autoload_preset));
    put32(temp,uint32_t(state.machine_snapshot.size()));
    temp.insert(temp.end(),state.machine_snapshot.begin(),state.machine_snapshot.end());
    put_string(temp,state.floppy_path); // appended; existing v4 layout untouched
    if(temp.size()>kMaxStateBytes) return false;
    out.swap(temp);
    return true;
}
inline bool decode_state(uint8_t const* data,size_t size,DecodedState& out)
{
    if(!data || size<12 || size>kMaxStateBytes) return false;
    size_t cursor=0; uint32_t magic=0,version=0,channel=0;
    if(!get32(data,size,cursor,magic) || !get32(data,size,cursor,version) ||
       !get32(data,size,cursor,channel) || magic!=kStateMagic || channel>16)
        return false;
    DecodedState tmp;
    tmp.value.midi_channel=channel;
    if(version==1) {
        if(size!=12) return false;
        tmp.midi_only_v1=true; out=std::move(tmp); return true;
    }
    if(version!=2 && version!=3 && version!=4 && version!=kStateVersion) return false;
    auto& s=tmp.value;
    if(!get_string(data,size,cursor,s.rom_dir,kMaxPath) ||
       !get_string(data,size,cursor,s.hd_path,kMaxPath) ||
       !get_string(data,size,cursor,s.cd_path,kMaxPath) ||
       !get_string(data,size,cursor,s.cem_model,8)) return false;
    for(size_t i=0;i<4;++i)
        if(!get32(data,size,cursor,s.extra_kind[i]) ||
           !get_string(data,size,cursor,s.extra_path[i],kMaxPath)) return false;
    for(auto& change : s.hd_changes)
        if(!get_string(data,size,cursor,change,kMaxDiskChanges)) return false;
    if(version>=3) {
        uint32_t bank=0,preset=0;
        if(!get32(data,size,cursor,bank) || !get32(data,size,cursor,preset))
            return false;
        auto valid_number=[](uint32_t v) { return v==UINT32_MAX || v<=99; };
        if(!valid_number(bank) || !valid_number(preset)) return false;
        s.autoload_bank=bank==UINT32_MAX?-1:int32_t(bank);
        s.autoload_preset=preset==UINT32_MAX?-1:int32_t(preset);
        tmp.has_autoload_v3=true;
    }
    if(version>=4) {
        uint32_t n=0;
        if(!get32(data,size,cursor,n) || n>kMaxMachineSnapshot || n>size-cursor) return false;
        s.machine_snapshot.assign(data+cursor,data+cursor+n);
        cursor+=n;
    }
    if(version>=5 && !get_string(data,size,cursor,s.floppy_path,kMaxPath))return false;
    if(cursor!=size || !valid_state(s)) return false;
    out=std::move(tmp); return true;
}
} // namespace eiii::vst3
