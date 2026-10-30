// B02: ROM-free production audio backend exact continuation (V1 and V2).
// The F-Chip RAM pointer must remain bound to the destination instance.
#include "host/eiii_backend.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>
using eiii::software_backend;

static void run(bool useV2) {
    std::vector<uint16_t> a(4096),b(4096);
    for(size_t i=0;i<a.size();++i) a[i]=uint16_t(int(i*131u%30000u)-15000);
    b=a;
    software_backend original(a.data(),a.size(),48000.,2);
    software_backend restored(b.data(),b.size(),48000.,2);
    if(useV2) {
        const eiii::cem_calibration nominal{};
        original.use_model_v2(nominal,4);
        restored.use_model_v2(nominal,4);
    }
    // Original F-Chip wave-loop registers, sequential firmware-style writes.
    // F-Chip DEC+LOOP: a genuine 256-word backwards loop. An
    // increasing counter of 255 is NOT a 256-word segment: it would
    // keep stepping toward 0x3fffff and eventually leave the 4K fixture.
    original.rf_write(0,0,0,0x018000ffu);
    original.rf_write(0,0,1,0x018000ffu);
    original.rf_write(0,0,2,0x008000ffu);
    original.rf_write(0,0,3,0x008000ffu);
    original.ctrl_write(0,0,0x00u);
    original.ctrl_write(0,0,0x40u); // 64 C20M ticks per sample
    for(unsigned bank=0;bank<5;++bank) {
        const uint16_t word[5]={0x2500,0x2700,0x2d00,0x2e00,0x2000};
        original.cv_write(0,bank,15,word[bank]); // channel 15 -> voice zero
    }
    original.render_until(160000);
    assert(original.lanes().state(0).active);
    assert(original.lanes().state(0).rate==64);
    assert(original.audio().size()>50 && original.audio().size()%2==0);
    assert(std::any_of(original.audio().begin(),original.audio().end(),
        [](float x){return std::abs(x)>1.0e-16f;}));

    software_backend::recall_state snapshot{};
    assert(original.capture_recall_state(snapshot));
    assert(software_backend::valid_recall_state(snapshot));
    assert(restored.restore_recall_state(snapshot));
    assert(restored.now()==original.now());
    assert(restored.audio()==original.audio());
    assert(restored.lanes().state(0).rf==original.lanes().state(0).rf);

    // Mid-voice continuation must produce exactly the same queued stereo
    // frames, including filter history, decimator phase and optional RNG.
    original.audio().clear();
    restored.audio().clear();
    original.render_until(320000);
    restored.render_until(320000);
    assert(original.audio().size()>50);
    assert(original.audio()==restored.audio());
    assert(original.lanes().state(0).rf==restored.lanes().state(0).rf);

    auto invalid=snapshot;invalid.next=std::numeric_limits<double>::quiet_NaN();
    const uint64_t originalNow=restored.now();
    assert(!restored.restore_recall_state(invalid));
    invalid=snapshot;invalid.model_v2=!invalid.model_v2;
    assert(!restored.restore_recall_state(invalid));
    invalid=snapshot;invalid.sample_words+=1;
    assert(!restored.restore_recall_state(invalid));
    invalid=snapshot;invalid.output.push_back(0.1f); // odd stereo frame
    assert(!restored.restore_recall_state(invalid));
    assert(restored.now()==originalNow);
    software_backend incompatible(b.data(),b.size(),44100.,2);
    assert(!incompatible.restore_recall_state(snapshot));

    // No F-Chip memcpy: the destination must retain its own RAM binding.
    const auto addr=restored.lanes().state(0).rf[2]&0x003fffffu;
    assert(addr<b.size());
    b[addr]=0x7fffu;
    assert(restored.lanes().pcm(0)==32767);
    assert(original.lanes().pcm(0)!=32767);
    std::printf("PASS B02: audio %s exact queued stereo/filter/voice continuation and independent F-Chip RAM binding\\n",
                useV2?"CEM V2 FIR":"CEM V1");
}

int main(){run(false);run(true);}
