// Synthetic public-domain fixtures generated in memory; never use proprietary ROM.
#include "../../upstream/runtime/wav_import.h"
#include "../../upstream/runtime/wav_place_key.h"
#include "../vst3-port/sample_input.h"
#include "../../upstream/runtime/adc_quantize.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <bit>
#include <cmath>
#include <cstring>
#include <future>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>
using namespace eiii::wav;
void le(std::vector<uint8_t>& b,uint32_t v,unsigned n=4) {for(unsigned i=0;i<n;++i)b.push_back(uint8_t(v>>(8*i)));}
void tag(std::vector<uint8_t>& b,const char* text) {b.insert(b.end(),text,text+4);}
std::vector<uint8_t> fixture(unsigned channels,unsigned format,unsigned rate) {
    const unsigned bits=format==1?16:format==2?24:32;
    std::vector<uint8_t> b;tag(b,"RIFF");le(b,0);tag(b,"WAVE");tag(b,"JUNK");le(b,1);b.push_back(7);b.push_back(0);
    tag(b,"fmt ");le(b,16);le(b,format==3?3:1,2);le(b,channels,2);le(b,rate);le(b,rate*channels*(bits/8));le(b,channels*(bits/8),2);le(b,bits,2);
    tag(b,"data");le(b,32*channels*(bits/8));
    for(unsigned i=0;i<32;++i)for(unsigned c=0;c<channels;++c) {
        // Leading silence and independent channels must survive decode.
        const float x=i<4?0.f:c==0?.5f:-.25f;
        if(bits==32)le(b,std::bit_cast<uint32_t>(x));
        else le(b,uint32_t(int32_t(x*(bits==16?32768:8388608))),bits/8);
    }
    const uint32_t n=uint32_t(b.size()-8);for(unsigned i=0;i<4;++i)b[4+i]=uint8_t(n>>(8*i));return b;
}
// These are synthetic diagnostic controls, NOT a reproduction of the user's
// original WAV/firmware run. The same lengths can hide different missing ranges.
void capture_diagnostic_regressions() {
    const auto require=[](bool condition,const char* what) {
        if(!condition)throw std::runtime_error(what); // active with NDEBUG too
    };
    constexpr size_t missing=52;
    for(size_t n:{size_t(128),size_t(44053),size_t(83029),size_t(352423)}) {
        std::vector<int16_t> l(n),r(n);
        for(size_t i=0;i<n;++i) {
            l[i]=int16_t(1+i%30000);r[i]=int16_t(-1-int(i%31000));
        }
        for(size_t gap:{size_t(0),n/3,n-missing}) {
            std::vector<uint16_t> memory;
            for(const auto* channel:{&l,&r})
                for(size_t i=0;i<n;++i)
                    if(i<gap||i>=gap+missing)memory.push_back(uint16_t((*channel)[i]));
            const auto diagnostic=diagnose_payload(memory,l,r);
            require(diagnostic.equal_halves,"even diagnostic layout");
            require(diagnostic.assumed_channel_words==n-missing,"short capture midpoint must not underflow");
            const size_t prefix=gap,suffix=n-missing-gap;
            const auto counts="deficit=52 equal_prefix="+std::to_string(prefix)+
                              " equal_suffix="+std::to_string(suffix);
            const auto first=diagnostic.summary.find(counts);
            require(first!=std::string::npos,"left prefix/suffix diagnostic");
            require(diagnostic.summary.find(counts,first+1)!=std::string::npos,
                    "right prefix/suffix diagnostic");
            CaptureLayout layout{1,2,3};
            const bool accepted=verify_payload(memory,l,r,{881,1000,2},&layout);
            require(!accepted,"missing PCM must remain rejected after diagnosis");
            require(layout.channel_words==0&&layout.pre==0&&layout.post==0,
                    "failed comparison must not publish a layout");
            if(n==83029) {
                require(memory.size()==165954,"reported total word count");
                require(diagnostic.assumed_channel_words==82977,"reported per-channel count");
            }
        }
        std::vector<uint16_t> complete;
        for(const auto* channel:{&l,&r})
            for(auto v:*channel)complete.push_back(uint16_t(v));
        const auto diagnostic=diagnose_payload(complete,l,r);
        const auto equal="extra=0 equal_prefix="+std::to_string(n)+
                         " equal_suffix="+std::to_string(n)+" first_difference=none";
        require(diagnostic.summary.find(equal)!=std::string::npos,"complete PCM diagnostic control");
        const bool accepted=verify_payload(complete,l,r,{});
        require(accepted,"diagnostics must not reject intact PCM");
        if(n==83029) {
            // A wrong extracted extent can report the same deficit while the
            // complete PCM still exists in its backing memory. Counts alone
            // cannot prove which channel or source interval was lost.
            const auto cropped=std::span<const uint16_t>(complete).first(165954);
            const auto extent=diagnose_payload(cropped,l,r);
            require(extent.assumed_channel_words==82977,"wrong-extent diagnostic control");
            require(!verify_payload(cropped,l,r,{}),"wrong extent must remain rejected");
            require(complete.size()==166058,"backing PCM remains complete");
        }
        complete.pop_back();
        const auto odd=diagnose_payload(complete,l,r);
        require(!odd.equal_halves,"odd extent cannot justify a planar boundary");
    }
    const auto empty=diagnose_payload({}, {}, {});
    require(empty.assumed_channel_words==0,"empty diagnostic extent");
    std::cout<<"PASS synthetic 52-sample prefix/middle/tail diagnostic controls (original-media test is separate)\n";
}
void forced_capture_regressions() {
    const auto require=[](bool condition,const char* what) {
        if(!condition)throw std::runtime_error(what);
    };
    for(size_t n:{size_t(1),size_t(128),size_t(83029)}) {
        std::vector<int16_t> l(n),r(n);
        for(size_t i=0;i<n;++i) {l[i]=i<40?0:int16_t(1+i%30000);r[i]=i<17?0:int16_t(-1-int(i%29000));}
        // Independently measured FORCE run of the real bass file: 14890 ADC
        // reads before the file, 26 after; one discarded read and four guards.
        const size_t before=14890,after=26,pre=14891,post=28;
        std::vector<uint16_t> memory;
        for(const auto* c:{&l,&r}) {
            memory.insert(memory.end(),pre,0);
            for(auto v:*c)memory.push_back(uint16_t(v));
            memory.insert(memory.end(),post,0);
        }
        CaptureLayout layout{};
        require(verify_forced_payload(memory,l,r,before,after,&layout),"complete FORCE source including silence");
        require(layout.pre==pre&&layout.post==post&&layout.channel_words==pre+n+post,"exact FORCE event/guard counts");
        require(!verify_forced_payload(memory,l,r,before-1,after),"wrong source-start ADC count");
        require(!verify_forced_payload(memory,l,r,before,after+1),"wrong stop ADC count");
        auto damaged=memory;damaged[pre]^=1;
        require(!verify_forced_payload(damaged,l,r,before,after),"first WAV value corruption");
        damaged=memory;damaged[pre+n-1]^=1;
        require(!verify_forced_payload(damaged,l,r,before,after),"last WAV value corruption");
        damaged=memory;damaged[pre+n/2]^=1;
        require(!verify_forced_payload(damaged,l,r,before,after),"middle WAV value corruption");
        damaged=memory;damaged[layout.channel_words+pre]^=1;
        require(!verify_forced_payload(damaged,l,r,before,after),"right channel corruption");
        damaged=memory;damaged[0]=1;
        require(!verify_forced_payload(damaged,l,r,before,after),"nonzero leading guard");
        damaged=memory;damaged.back()=1;
        require(!verify_forced_payload(damaged,l,r,before,after),"nonzero trailing guard");
        if(n==83029) {
            // Exact original failure structure, proven from the private trace:
            // 2 guards, source[83..83029), 27 ADC zeros, 2 end guards per channel.
            std::vector<uint16_t> threshold;
            for(const auto* c:{&l,&r}) {
                threshold.insert(threshold.end(),2,0);
                for(size_t i=83;i<n;++i)threshold.push_back(uint16_t((*c)[i]));
                threshold.insert(threshold.end(),29,0);
            }
            require(threshold.size()==165954,"original threshold failure extent");
            require(!verify_forced_payload(threshold,l,r,881,27),"original threshold loss must stay rejected");
            damaged=memory;damaged.erase(damaged.begin()+std::ptrdiff_t(pre+7));damaged.push_back(0);
            require(!verify_forced_payload(damaged,l,r,before,after),"deleted source sample with compensated length");
            std::vector<uint16_t> swapped(memory.begin()+std::ptrdiff_t(layout.channel_words),memory.end());
            swapped.insert(swapped.end(),memory.begin(),memory.begin()+std::ptrdiff_t(layout.channel_words));
            require(!verify_forced_payload(swapped,l,r,before,after),"swapped stereo channels");
        }
        std::fill(l.begin(),l.end(),0);std::fill(r.begin(),r.end(),0);std::fill(memory.begin(),memory.end(),0);
        require(verify_forced_payload(memory,l,r,before,after),"silence has a unique event-defined position");
        require(!verify_forced_payload(memory,l,r,std::numeric_limits<size_t>::max(),after),"pre-count overflow");
        require(!verify_forced_payload(memory,l,r,before,std::numeric_limits<size_t>::max()),"post-count overflow");
    }
    std::cout<<"PASS exact FORCE timing/guards, original 83-prefix-loss structure, stereo and corruption controls\n";
}
int main() {
    capture_diagnostic_regressions();
    forced_capture_regressions();
    unsigned cases=0;
    for(unsigned channels:{1u,2u})for(unsigned format:{1u,2u,3u})for(unsigned rate:{22050u,44100u,48000u,96000u,192000u}) {
        Audio a;std::string error;auto bytes=fixture(channels,format,rate);
        assert(decode(bytes,a,error) && error.empty());
        assert(a.channels==channels && a.rate==rate && a.frames()==32);
        assert(a.pcm[0]==0 && a.pcm[3*channels]==0);
        assert(a.pcm[4*channels]==.5f);
        if(channels==2)assert(a.pcm[4*channels+1]==-.25f);
        assert(std::fabs(a.duration()-32./rate)<1e-12);
        assert(a.at(-1,0)==0 && a.at(a.duration()+1,0)==0);
        assert(a.at(8./rate,0)==.5f);
        assert(a.at(8./rate,1)==(channels==2?-.25f:.5f));
        // Existing sample-input adapter remains the source boundary contract.
        float left[4]={a.at(8./rate,0),0,0,0},right[4]={a.at(8./rate,1),0,0,0};
        const float* inputs[]={left,right};float l[4]{},r[4]{};
        eiii::vst3::SampleInput s;s.channels=channels;s.data32=inputs;
        eiii::vst3::copy_sample_input(s,l,r,4);
        assert(eiii::adc::quantize(l[0],1)==16384);
        assert(eiii::adc::quantize(r[0],1)==(channels==2?-8192:16384));
        const Audio before=a;
        auto truncated=bytes;truncated.pop_back();assert(!decode(truncated,a,error));assert(a.pcm==before.pcm);
        auto invalid=bytes;invalid[32]=7;assert(!decode(invalid,a,error)); // unsupported channel count
        assert(a.pcm==before.pcm);
        ++cases;
    }
    assert((Options{60,36,84,0}).valid());assert(!(Options{60,61,84,0}).valid());assert(!(Options{60,0,128,0}).valid());assert(!(Options{60,0,127,100}).valid());
    Audio a;std::string error;auto b=fixture(2,3,48000);assert(decode(b,a,error));
    const Audio before=a;
    // Last payload float: NaN, infinity and finite overload all fail transactionally.
    for(float value:{std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity(),1.01f,-1.01f}) {
        auto damaged=b;const auto bits=std::bit_cast<uint32_t>(value);
        for(unsigned k=0;k<4;++k)damaged[damaged.size()-4+k]=uint8_t(bits>>(8*k));
        assert(!decode(damaged,a,error));assert(a.pcm==before.pcm);
    }
    assert(!fits(a,16));assert(fits(a));
    Audio invalidCapacity=a;invalidCapacity.rate=0;assert(!fits(invalidCapacity));
    invalidCapacity=a;invalidCapacity.pcm.push_back(0);assert(!fits(invalidCapacity));
    // The real 8 MiB ceiling applies to mono too: firmware records dual ADCs.
    Audio tooLong;tooLong.rate=8000;tooLong.channels=1;tooLong.pcm.resize(8000*48);
    assert(!fits(tooLong));
    // Concurrent instances must retain independent source buffers and cursors.
    auto decode_instance=[](unsigned rate) {Audio x;std::string e;assert(decode(fixture(2,2,rate),x,e));return x;};
    auto first=std::async(std::launch::async,decode_instance,44100),second=std::async(std::launch::async,decode_instance,96000);
    const auto one=first.get(),two=second.get();assert(one.rate==44100 && two.rate==96000 && one.pcm==two.pcm);
    assert(!load("125A-NO-SUCH-WAV.wav",a,error));assert(a.pcm==before.pcm);
    // Firmware v8 measured equal planar blocks: 251 pre-roll zero values,
    // 44,053 complete per-channel PCM values, 21 final zeros per channel.
    // A timing-constrained validator must reject displacements, corruption
    // and unilateral channel padding rather than guessing a right boundary.
    std::vector<int16_t> left={11,21,31,44,58},right={-7,-17,-27,-38,-49};
    const CaptureWindow observed{881,19,2};
    std::vector<uint16_t> payload;
    auto block=[&](const std::vector<int16_t>& values,size_t pre,size_t post){
        payload.insert(payload.end(),pre,0);
        for(auto v:values)payload.push_back(uint16_t(v));
        payload.insert(payload.end(),post,0);
    };
    block(left,251,21);block(right,251,21);
    CaptureLayout layout{};
    assert(verify_payload(payload,left,right,observed,&layout));
    assert(layout.channel_words==277 && layout.pre==251 && layout.post==21);
    auto changed=payload;changed[251]^=1;assert(!verify_payload(changed,left,right,observed));
    changed=payload;changed[1]=1;assert(!verify_payload(changed,left,right,observed));
    changed=payload;changed[276]=1;assert(!verify_payload(changed,left,right,observed));
    changed=payload;changed[277+251]^=1;assert(!verify_payload(changed,left,right,observed));
    changed=payload;changed[277+252]=0;assert(!verify_payload(changed,left,right,observed));
    changed=payload;changed.insert(changed.begin()+277,0);
    assert(!verify_payload(changed,left,right,observed)); // unequal channel lengths
    changed=payload;changed.insert(changed.end(),4,0);
    assert(!verify_payload(changed,left,right,observed)); // right tail exceeds ADC + guard
    changed=payload;changed[277+251]=uint16_t(left[0]);
    assert(!verify_payload(changed,left,right,observed)); // channel swap/displacement
    assert(!verify_payload(payload,left,right,{250,19,2})); // overlong unaccounted pre-roll
    assert(!verify_payload(payload,left,right,{881,19,1})); // unexplained end guard
    std::vector<int16_t> silence(5,0);
    assert(!verify_payload(std::vector<uint16_t>(2*277,0),silence,silence,observed)); // unknown start for silent file
    // Select key fields by their firmware prompt, not by the first
    // note anywhere on the display (the unchanged Original Key would
    // otherwise mask Low/High). EIII LCD: C2=48, C3=60, C4=72, C5=84.
    {
        using eiii::wav::PlaceKeyStage;
        unsigned midi=999;
        const std::string original="    PLACE SAMPLE    \nP00 pri C4                  \nSelect Original Key\n                  \n";
        const std::string low="    PLACE SAMPLE    \nP00 pri C4  C3              \nSelect Low Key\n                  \n";
        const std::string high="    PLACE SAMPLE    \nP00 pri C4  C3 to C5        \nSelect High Key\n                  \n";
        assert(parse_place_key(original,PlaceKeyStage::original,midi)&&midi==72);
        assert(parse_place_key(low,PlaceKeyStage::low,midi)&&midi==60);
        assert(parse_place_key(high,PlaceKeyStage::high,midi)&&midi==84);
        assert(!parse_place_key(low,PlaceKeyStage::high,midi));
        assert(!parse_place_key("PLACE SAMPLE\nP00 pri C4\nSelect Low Key\n",
                                PlaceKeyStage::low,midi));
        assert(!parse_place_key("PLACE SAMPLE\nNo Primary Sample\nSelect Original Key\n",
                                PlaceKeyStage::original,midi));
        // REAL v11 Windows log (19.10.2026): firmware hides the prompt
        // after the DATA slider moves, while "pri C7" remains valid.
        const std::string actual_start=
            "    PLACE SAMPLE                        \n"
            "pri G2              Select Original Key\n"
            "No Secondary Sample                   \n"
            "                                        \n";
        const std::string actual_changed=
            "    PLACE SAMPLE    No Primary Sample   \n"
            "pri C7              No Secondary Sample \n"
            "........................................\n"
            "D...........r........\\!@................\n";
        assert(parse_place_key(actual_start,PlaceKeyStage::original,midi)&&midi==55);
        assert(parse_place_key(actual_changed,PlaceKeyStage::original,midi)&&midi==108);
        assert(!parse_place_key(actual_changed,PlaceKeyStage::low,midi));
        assert(!parse_place_key(actual_changed,PlaceKeyStage::high,midi));
        // The reference manual shows three distinct stage layouts.
        // They remain parseable when the status text replaces the prompt.
        const std::string real_low=
            "    PLACE SAMPLE    No Primary Sample\n"
            "pri C4  C3         No Secondary Sample\n"
            "........................................\n"
            "........................................\n";
        const std::string real_high=
            "    PLACE SAMPLE    No Primary Sample\n"
            "pri C4  C3 to C5   No Secondary Sample\n"
            "........................................\n"
            "........................................\n";
        assert(parse_place_key(real_low,PlaceKeyStage::low,midi)&&midi==60);
        assert(parse_place_key(real_high,PlaceKeyStage::high,midi)&&midi==84);
        assert(!parse_place_key(real_low,PlaceKeyStage::original,midi));
        assert(!parse_place_key(real_high,PlaceKeyStage::low,midi));
        // v16 real MIDI/Edit Assignment probe: MIDI 48=C2, 60=C3,
        // 72=C4, 84=C5. Select PLACE SAMPLE fields using that same
        // EIII octave convention. The formerly accepted C4=60 is WRONG.
        const std::string mappedOriginal=
            "    PLACE SAMPLE    No Primary Sample   \n"
            "pri C3              No Secondary Sample \n"
            "........................................\n";
        const std::string mappedLow=
            "    PLACE SAMPLE    No Primary Sample   \n"
            "pri C3  C2          No Secondary Sample \n"
            "........................................\n";
        const std::string mappedHigh=
            "    PLACE SAMPLE    No Primary Sample   \n"
            "pri C3  C2  to C4   No Secondary Sample \n"
            "........................................\n";
        assert(parse_place_key(mappedOriginal,PlaceKeyStage::original,midi)&&midi==60);
        assert(parse_place_key(mappedLow,PlaceKeyStage::low,midi)&&midi==48);
        assert(parse_place_key(mappedHigh,PlaceKeyStage::high,midi)&&midi==72);
        const std::string outOfRange=
            "    PLACE SAMPLE    No Primary Sample\n"
            "pri C9              No Secondary Sample\n";
        assert(!parse_place_key(outOfRange,PlaceKeyStage::original,midi));

        assert(!parse_place_key("OTHER MODULE\npri C7\n...\n",PlaceKeyStage::original,midi));
        assert(!parse_place_key("PLACE SAMPLE\npri C10\n...\n",PlaceKeyStage::original,midi));

    }

    // Measured firmware v13/v16: Original Key C4 (MIDI 72) at ADC 0x4154; the same
    // ADC shows F3 when Select Low Key starts. ADC 0x0000 yields raw
    // LCD [41 00], i.e. an invalid note, and must NOT be a search origin.
    {
        const std::string rawInvalid=std::string("    PLACE SAMPLE    No Primary Sample   \npri C4  A")+
                                     std::string(1,char(0))+
                                     "          No Secondary Sample \n";
        unsigned parsed=999;
        assert(!parse_place_key(rawInvalid,PlaceKeyStage::low,parsed));
        KeySeekResult result{};
        std::string reason;
        auto original=[](uint16_t adc,unsigned& note)->bool{
            note=55+unsigned(adc)/984;return note<=108;
        };
        assert(seek_key_adc(1,55,72,original,result,reason)&&result.note==72);
        // v14 REAL FAILURE: EIII G2 at ADC 1 was only a firmware DEFAULT.
        // Every measured step NEAR ZERO yielded A + NUL;
        // this does not establish a boundary at unmeasured interior values.
        const auto measured_original=[](uint16_t adc,unsigned& note)->bool{
            if(adc<0x2000)return false;
            // Firmware-measured anchors at 0x3f90 -> MIDI70 and
            // 0x4154 -> MIDI72, extrapolated ONLY in this synthetic test.
            const int n=72+(int(adc)-0x4154)/226;
            if(n<0||n>127)return false;
            note=unsigned(n);
            return true;
        };
        assert(!seek_key_adc(1,55,60,measured_original,result,reason));
        std::vector<unsigned> seen;
        const auto tracked_original=[&](uint16_t adc,unsigned& note)->bool{
            seen.push_back(adc);
            return measured_original(adc,note);
        };
        assert(seek_key_measured(60,tracked_original,result,reason));
        assert(result.note==60 && result.adc>0x2000 && result.adc<0x4154);
        assert(seen.size()>2 && seen[0]==0x3f90 && seen[1]==0x4154);
        // Explicitly reject a broken firmware key field at either
        // calibration anchor without moving to the physical endpoint.
        const auto broken_at_lower=[](uint16_t adc,unsigned& note)->bool{
            if(adc==0x3f90)return false;
            note=60;return true;
        };
        assert(!seek_key_measured(60,broken_at_lower,result,reason));
        const auto reversed_anchors=[](uint16_t adc,unsigned& note)->bool{
            note=adc==0x3f90?62:58;return true;
        };
        assert(!seek_key_measured(60,reversed_anchors,result,reason));
        // Synthetic Low-Key model has an invalid lower boundary but a
        // reachable C3; no probe may fall in the invalid ADC region.
        bool touched_invalid=false;
        auto low=[&](uint16_t adc,unsigned& note)->bool{
            if(adc<0x1500){touched_invalid=true;return false;}
            const int n=53+(int(adc)-0x4154)/700;
            if(n<0||n>127)return false;
            note=unsigned(n);return true;
        };
        assert(seek_key_adc(0x4154,53,48,low,result,reason)&&result.note==48);
        assert(!touched_invalid);
        auto high=[](uint16_t adc,unsigned& note)->bool{
            const int n=64+(int(adc)-0x4000)/700;
            if(n<0||n>127)return false;
            note=unsigned(n);return true;
        };
        assert(seek_key_adc(0x4000,64,72,high,result,reason)&&result.note==72);
        auto backwards=[](uint16_t adc,unsigned& note)->bool{
            note=70-unsigned(adc/500);return true;
        };
        assert(!seek_key_adc(0x1000,62,72,backwards,result,reason));
        assert(reason.find("nonmonotonic")!=std::string::npos);
        auto invalid=[](uint16_t adc,unsigned& note)->bool{
            if(adc<0x3000)return false;
            note=50+(unsigned(adc)-0x3000)/1000;return true;
        };
        assert(!seek_key_adc(0x3100,50,40,invalid,result,reason));
        auto flat=[](uint16_t,unsigned& note)->bool{note=55;return true;};
        assert(!seek_key_adc(0x3500,55,40,flat,result,reason));
        assert(!seek_key_adc(0x3500,55,128,flat,result,reason));
    }

    // Near-unity EIII ADC adaptation must never create intersample peaks.
    // Exercise PCM16-like full-scale transients at 44.1 kHz -> 44.0529 kHz.
    Audio nearUnity; nearUnity.rate=44100;nearUnity.channels=2;
    nearUnity.pcm.resize(44100*2);
    for(size_t i=0;i<nearUnity.frames();++i) {
        const float v=(i%29==0)?0.975f:((i%29==1)?-0.93f:0.0f);
        nearUnity.pcm[2*i]=v;
        nearUnity.pcm[2*i+1]=-v;
    }
    float highest=0;
    const double adcHz=10000000./227.;
    for(size_t i=0;i<size_t(adcHz);++i)
        for(unsigned c=0;c<2;++c)
            highest=std::max(highest,std::abs(nearUnity.filtered_at(double(i)/adcHz,c,adcHz)));
    assert(highest<=0.97501f);
    // Different sample-rate conversions still require antialiasing.
    // 96k -> ADC downsampling: suppress a 30kHz input above ADC Nyquist.
    Audio highTone;highTone.rate=96000;highTone.channels=1;highTone.pcm.resize(9600);
    for(size_t i=0;i<highTone.frames();++i)highTone.pcm[i]=float(.5*std::sin(2*3.141592653589793*30000*i/96000));
    double power=0;
    for(unsigned i=100;i<1000;++i){const auto x=highTone.filtered_at(i/(10000000./227.),0,10000000./227.);power+=x*x;}
    assert(std::sqrt(power/900)<.01);
    // Deterministic hostile chunk/metadata mutations under ASan/UBSan too.
    uint32_t random=0x125a;
    for(unsigned trial=0;trial<2000;++trial){
        auto mutated=b;
        for(unsigned k=0;k<4;++k){random=random*1664525u+1013904223u;mutated[random%mutated.size()]^=uint8_t(random>>24);}
        Audio checked;std::string why;
        if(decode(mutated,checked,why)){assert(fits(checked));for(float v:checked.pcm)assert(std::isfinite(v)&&std::abs(v)<=1);}
    }
    std::cout<<"PASS "<<cases<<" format/channel/rate cases; leading silence, ADC values, bounds, failures and independent instances\n";
}
