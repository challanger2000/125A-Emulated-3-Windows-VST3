#include "../vst3-port/sample_input.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>

int main()
{
    using namespace eiii::vst3;
    float l[8]{},r[8]{};
    const float l32[]={0.25f,-0.5f,1.f,0.f};
    const float r32[]={-0.75f,0.5f,0.1f,-0.1f};
    const float* stereo32[]={l32,r32};
    SampleInput s{};
    s.channels=2; s.data32=stereo32;
    copy_sample_input(s,l,r,4);
    assert(l[0]==0.25f && l[1]==-0.5f && l[2]==1.f);
    assert(r[0]==-0.75f && r[3]==-0.1f);

    s.channels=1;
    copy_sample_input(s,l,r,4);
    for(int i=0;i<4;++i) assert(l[i]==r[i] && l[i]==l32[i]);

    s.channels=2; s.silence_flags=2;
    copy_sample_input(s,l,r,4);
    for(int i=0;i<4;++i) assert(l[i]==l32[i] && r[i]==0.f);

    s.channels=2; s.silence_flags=1;
    copy_sample_input(s,l,r,4);
    for(int i=0;i<4;++i) assert(l[i]==0.f && r[i]==r32[i]);

    s.channels=0; s.silence_flags=0;
    copy_sample_input(s,l,r,4);
    for(int i=0;i<4;++i) assert(l[i]==0.f && r[i]==0.f);

    const double l64[]={0.625,-0.125,0.125,0.0};
    const double r64[]={-0.375,1.25,0.25,-0.5};
    const double* stereo64[]={l64,r64};
    s.channels=2; s.is64=true; s.data64=stereo64;
    copy_sample_input(s,l,r,4);
    assert(l[0]==0.625f && l[1]==-0.125f);
    assert(r[0]==-0.375f && r[1]==1.25f);

    const double bad64[]={std::numeric_limits<double>::quiet_NaN(),
                          std::numeric_limits<double>::infinity(),
                          -std::numeric_limits<double>::infinity(), 1e300};
    const double* badPtrs[]={bad64,r64};
    s.data64=badPtrs;
    copy_sample_input(s,l,r,4);
    for(int i=0;i<4;++i) assert(l[i]==0.f && r[i]==float(r64[i]));

    const float bad32[]={std::numeric_limits<float>::quiet_NaN(),
                         std::numeric_limits<float>::infinity(),
                         -std::numeric_limits<float>::infinity(),0.375f};
    const float* bad32Ptrs[]={bad32,r32};
    s.is64=false; s.data32=bad32Ptrs; s.channels=2;
    copy_sample_input(s,l,r,4);
    assert(l[0]==0.f && l[1]==0.f && l[2]==0.f && l[3]==0.375f);
    for(int i=0;i<4;++i) assert(r[i]==r32[i]);

    // Missing right buffer must not read freed/null memory.
    const float* monoPtrs[]={l32,nullptr};
    s.data32=monoPtrs;
    copy_sample_input(s,l,r,4);
    for(int i=0;i<4;++i) assert(l[i]==l32[i] && r[i]==0.f);

    // Mono source: a silent first channel must also mute the mirrored right ADC.
    s.channels=1; s.silence_flags=1; s.is64=false; s.data32=monoPtrs;
    copy_sample_input(s,l,r,4);
    for(int i=0;i<4;++i) assert(l[i]==0.f && r[i]==0.f);

    // Missing buffers in an advertised stereo bus must be treated as silence.
    s.channels=2; s.silence_flags=0; s.data32=nullptr;
    copy_sample_input(s,l,r,4);
    for(int i=0;i<4;++i) assert(l[i]==0.f && r[i]==0.f);
    s.is64=true; s.data64=nullptr;
    copy_sample_input(s,l,r,4);
    for(int i=0;i<4;++i) assert(l[i]==0.f && r[i]==0.f);

    // Invalid 64-bit conversion must never produce infinity at the ADC.
    const double extremes[]={1e300,-1e300,0.5,-0.5};
    const double* wide[]={extremes,extremes};
    s.data64=wide;
    copy_sample_input(s,l,r,4);
    assert(l[0]==0.f && r[0]==0.f);
    assert(l[1]==0.f && r[1]==0.f);
    assert(l[2]==0.5f && r[3]==-0.5f);

    // Zero-frame host process calls must not write to either ADC destination.
    l[0]=123.f; r[0]=456.f;
    copy_sample_input(s,l,r,0);
    assert(l[0]==123.f && r[0]==456.f);


    // Continuous stereo ADC input across uneven host blocks. Detect dropped,
    // repeated or reordered frames at a VST3 sample-block boundary.
    constexpr int total=1025;
    float adcL[total]{},adcR[total]{};
    float srcL[total]{},srcR[total]{};
    for(int i=0;i<total;++i) {
        srcL[i]=float(i)/float(total);
        srcR[i]=-float(i)/float(total);
    }
    const int blocks[]={1,64,3,257,17,511,172};
    int offset=0;
    for(const int frames : blocks) {
        const float* current[]={srcL+offset,srcR+offset};
        SampleInput input{};
        input.channels=2; input.data32=current;
        copy_sample_input(input,adcL+offset,adcR+offset,size_t(frames));
        offset+=frames;
    }
    assert(offset==total);
    for(int i=0;i<total;++i) {
        assert(adcL[i]==srcL[i] && adcR[i]==srcR[i]);
    }

    std::puts("EIII Sample In: 32/64-bit, stereo/mono, bus silence, NaN/Inf PASS");
    return 0;
}
