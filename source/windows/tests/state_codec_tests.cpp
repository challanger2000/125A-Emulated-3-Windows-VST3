#include "../vst3-port/project_state.h"
#include "../vst3-port/core_state_envelope.h"
#include <cstdio>
#include "../../upstream/runtime/eiii_runtime.h"
#ifdef NDEBUG
#undef NDEBUG // Keep assertions enabled in Release/CI tests.
#endif
#include <cassert>
#include <cstdio>
#include <vector>
using namespace eiii::vst3;
int main()
{
    // V2 is the default only for NEW instruments/projects. Saved explicit
    // V1 choices remain authoritative across all supported state versions.
    assert(ProjectState{}.cem_model=="v2");
    assert(eiii::runtime_config{}.cem_model=="v2");
    ProjectState a;
    assert(a.cem_model=="v2");
    a.midi_channel=14;
    a.autoload_bank=0;
    a.autoload_preset=1;
    a.rom_dir="C:\\EIII\\ROM";
    a.hd_path="C:\\EIII\\OS 2.42.img";
    a.cd_path="C:\\EIII\\Disc.iso";
    a.floppy_path="C:\\EIII\\Disk.img";
    a.extra_kind={1,2,0,1};
    a.extra_path={"D:\\Banks.img","", "", "E:\\Samples.img"};
    a.hd_changes[0]=std::string("a\0b\0\xff",5);
    a.hd_changes[1]=std::string(6000,'x');
    std::vector<uint8_t> bytes;
    assert(encode_state(a,bytes));
    DecodedState b;
    assert(decode_state(bytes.data(),bytes.size(),b));
    assert(!b.midi_only_v1 && b.value.hd_path==a.hd_path);
    assert(b.value.hd_changes==a.hd_changes && b.value.extra_kind==a.extra_kind);
    assert(b.value.midi_channel==14);
    assert(b.has_autoload_v3 && b.value.autoload_bank==0 && b.value.autoload_preset==1);

    assert(b.value.cem_model=="v2"); // fresh state serializes V2
    ProjectState savedV1=a;
    savedV1.cem_model="v1";
    std::vector<uint8_t> explicitV1;
    assert(encode_state(savedV1,explicitV1));
    assert(decode_state(explicitV1.data(),explicitV1.size(),b));
    assert(b.value.cem_model=="v1"); // must not reinterpret existing songs

    auto damaged=bytes;
    damaged[0]=0;
    assert(!decode_state(damaged.data(),damaged.size(),b));
    damaged=bytes; damaged[4]=6;
    assert(!decode_state(damaged.data(),damaged.size(),b)); // unsupported future version
    damaged=bytes; damaged[8]=17;
    assert(!decode_state(damaged.data(),damaged.size(),b)); // invalid channel
    damaged=bytes; damaged.push_back(0);
    assert(!decode_state(damaged.data(),damaged.size(),b)); // trailing bytes
    assert(!decode_state(bytes.data(),bytes.size()-1,b)); // truncated
    damaged=bytes; damaged[12]=0xff;damaged[13]=0xff;
    assert(!decode_state(damaged.data(),damaged.size(),b)); // invalid path length

    // v2 is the previous full-media state (no recall fields).
    auto v2=bytes;
    v2[4]=2;
    v2.resize(v2.size()-16-a.floppy_path.size());
    assert(decode_state(v2.data(),v2.size(),b));
    assert(!b.has_autoload_v3 && !b.midi_only_v1);
    assert(b.value.hd_changes==a.hd_changes && b.value.autoload_bank==-1);

    assert(b.value.cem_model=="v2");
    auto legacyV2=explicitV1;
    legacyV2[4]=2;
    legacyV2.resize(legacyV2.size()-16-savedV1.floppy_path.size());
    assert(decode_state(legacyV2.data(),legacyV2.size(),b));
    assert(b.value.cem_model=="v1"); // old V2-format song explicitly V1
    assert(!decode_state(bytes.data(),bytes.size()-1,b)); // v3 recall fields truncated

    ProjectState invalid=a;
    invalid.autoload_bank=100;
    assert(!encode_state(invalid,damaged));
    invalid=a; invalid.autoload_preset=99; invalid.autoload_bank=-1;
    assert(!encode_state(invalid,damaged));
    invalid=a; invalid.extra_kind[0]=9;
    assert(!encode_state(invalid,damaged));
    invalid=a; invalid.extra_path[2]="unexpected";
    assert(!encode_state(invalid,damaged));
    invalid=a; invalid.hd_changes[0]=std::string(kMaxDiskChanges+1,'z');
    assert(!encode_state(invalid,damaged));

    // Version 3 retains autoload but does not carry the B02 snapshot field.
    auto v3=bytes;
    v3[4]=3;
    v3.resize(v3.size()-8-a.floppy_path.size());
    assert(decode_state(v3.data(),v3.size(),b));
    assert(b.has_autoload_v3 && b.value.machine_snapshot.empty());
    // Version 4 machine snapshot is a bounded opaque byte payload.
    ProjectState withMachine=a;
    withMachine.machine_snapshot={1,2,0,255,7};
    assert(encode_state(withMachine,damaged));
    assert(decode_state(damaged.data(),damaged.size(),b));
    assert(b.value.machine_snapshot==withMachine.machine_snapshot);
    assert(b.value.floppy_path==withMachine.floppy_path);
    assert(!decode_state(damaged.data(),damaged.size()-1,b));
    assert(b.value.machine_snapshot==withMachine.machine_snapshot);
    // A forged V4 machine length must be rejected before any state mutation.
    auto badMachineLength=damaged;
    const size_t machineLengthPos=badMachineLength.size()-withMachine.machine_snapshot.size()-8-withMachine.floppy_path.size();
    for(size_t i=0;i<4;++i) badMachineLength[machineLengthPos+i]=0xff;
    assert(!decode_state(badMachineLength.data(),badMachineLength.size(),b));
    assert(b.value.machine_snapshot==withMachine.machine_snapshot);

    // A v4 project predates D0 and must decode with no mounted floppy.
    auto v4=bytes;v4[4]=4;v4.resize(v4.size()-4-a.floppy_path.size());
    assert(decode_state(v4.data(),v4.size(),b));
    assert(b.value.floppy_path.empty());
    assert(b.value.hd_path==a.hd_path);
    // Version 1 was a 12-byte native little-endian MIDI-only state.
    std::vector<uint8_t> v1;
    put32(v1,kStateMagic); put32(v1,1); put32(v1,5);
    assert(decode_state(v1.data(),v1.size(),b));
    assert(b.midi_only_v1 && !b.has_autoload_v3 && b.value.midi_channel==5);
    assert(b.value.cem_model=="v2"); // legacy MIDI-only state has no analog model
    assert(b.value.hd_path.empty());
    assert(!decode_state(v1.data(),v1.size()-1,b));
    // Regression: invalid restore must not mutate the last valid decoded state.
    assert(decode_state(bytes.data(),bytes.size(),b));
    const auto validPath=b.value.hd_path;
    const auto validChanges=b.value.hd_changes;
    auto invalidRestore=bytes;
    invalidRestore.resize(invalidRestore.size()-2);
    assert(!decode_state(invalidRestore.data(),invalidRestore.size(),b));
    assert(b.value.hd_path==validPath && b.value.hd_changes==validChanges);

    // All supported MIDI target values, including host passthrough and channel 16.
    for(uint32_t channel=0;channel<=16;++channel) {
        ProjectState roundtrip=a;
        roundtrip.midi_channel=channel;
        assert(encode_state(roundtrip,damaged));
        assert(decode_state(damaged.data(),damaged.size(),b));
        assert(b.value.midi_channel==channel);
    }

    // Disk changes are binary blobs (including zero bytes), not text.
    ProjectState binary=a;
    binary.hd_changes[4]=std::string("\\0\\xff\\0\\x01",4);
    assert(encode_state(binary,damaged));
    assert(decode_state(damaged.data(),damaged.size(),b));
    assert(b.value.hd_changes[4]==binary.hd_changes[4]);

    // Reject embedded NUL in host paths, including names of extra images.
    ProjectState badPath=a;
    badPath.floppy_path=std::string("invalid\0floppy",14);
    assert(!encode_state(badPath,damaged));
    badPath=a;
    badPath.extra_path[0]=std::string("disk\0.img",9);
    assert(!encode_state(badPath,damaged));

    // B02 envelope regression: roundtrip and non-destructive rejection.
    {
        std::vector<uint8_t> payload{0,1,255,0,5}, encoded, decoded{42};
        assert(CoreStateEnvelope::encode(payload,encoded));
        assert(CoreStateEnvelope::decode(encoded.data(),encoded.size(),decoded));
        assert(decoded==payload);
        encoded.back()^=1;
        assert(!CoreStateEnvelope::decode(encoded.data(),encoded.size(),decoded));
        assert(decoded==payload);
        encoded.back()^=1;
        assert(!CoreStateEnvelope::decode(encoded.data(),encoded.size()-1,decoded));
        encoded[4]=2;
        assert(!CoreStateEnvelope::decode(encoded.data(),encoded.size(),decoded));
        assert(decoded==payload);
    }
    std::puts("Project state: fresh CEM V2 default, old explicit V1 preserved, v1/v2/v3/v4 recall and corruption PASS");
}
