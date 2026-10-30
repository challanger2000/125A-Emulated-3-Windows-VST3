#include "../standalone/shim/utf8_file_paths.h"
#include "wav_import.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>

namespace eiii::wav {
namespace {
uint16_t u16(const uint8_t* p) { return uint16_t(p[0] | uint16_t(p[1]) << 8); }
uint32_t u32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }
}
bool fits(const Audio& a, uint64_t available) {
    if ((a.channels != 1 && a.channels != 2) || a.rate < 8000 || a.rate > 384000 || !a.frames() || a.pcm.size() % a.channels) return false;
    // ADC maximum is 10 MHz / 227; reserve 0.5 s for firmware stop timing.
    const long double frames = std::ceil((static_cast<long double>(a.frames()) / a.rate + .5L) * (10000000.L / 227.L));
    return frames * 2 * 2 + 4096 <= available;
}
bool decode(std::span<const uint8_t> b, Audio& out, std::string& error) {
    auto fail = [&](const char* why) { error = why; return false; };
    if (b.size() < 12 || b.size() > max_file_bytes || std::memcmp(b.data(), "RIFF", 4) || std::memcmp(b.data()+8, "WAVE", 4))
        return fail("Expected bounded RIFF/WAVE file");
    const uint64_t end = uint64_t(u32(b.data()+4)) + 8;
    if (end != b.size()) return fail("Truncated or trailing RIFF data");
    const uint8_t* fmt = nullptr; const uint8_t* data = nullptr;
    uint32_t fmt_size = 0, data_size = 0;
    for (size_t p = 12; p < b.size();) {
        if (b.size()-p < 8) return fail("Truncated WAV chunk header");
        const uint32_t n = u32(b.data()+p+4);
        const uint64_t next = uint64_t(p) + 8 + n + (n & 1u);
        if (next > b.size()) return fail("Truncated WAV chunk");
        if (!std::memcmp(b.data()+p, "fmt ", 4)) {
            if (fmt) return fail("Duplicate WAV format");
            fmt = b.data()+p+8; fmt_size = n;
        } else if (!std::memcmp(b.data()+p, "data", 4)) {
            if (data) return fail("Duplicate WAV audio data");
            data = b.data()+p+8; data_size = n;
        }
        p = size_t(next);
    }
    if (!fmt || fmt_size < 16 || !data || !data_size) return fail("Missing WAV format or audio data");
    const unsigned format=u16(fmt), channels=u16(fmt+2), bits=u16(fmt+14), align=u16(fmt+12);
    const uint32_t rate=u32(fmt+4);
    if ((channels!=1 && channels!=2) || rate<8000 || rate>384000) return fail("Use mono/stereo WAV at 8000..384000 Hz");
    if (!((format==1 && (bits==16 || bits==24)) || (format==3 && bits==32))) return fail("Supported WAV encodings: PCM16, PCM24, IEEE Float32");
    if (align != channels*(bits/8) || uint64_t(rate)*align != u32(fmt+8) || data_size%align) return fail("Inconsistent WAV block alignment or byte rate");
    const size_t frames=data_size/align;
    // Bound allocation before decoding, including high-rate source files.
    Audio a; a.rate=rate; a.channels=channels;
    if (std::ceil((static_cast<long double>(frames)/rate+.5L)*(10000000.L/227.L))*2*2+4096>sample_ram_bytes)
        return fail("WAV exceeds EIII sample RAM capacity");
    a.pcm.resize(frames*channels);
    for (size_t i=0; i<a.pcm.size(); ++i) {
        const auto* p=data+i*(bits/8);
        float value=0;
        if (format==3) value=std::bit_cast<float>(u32(p));
        else if(bits==16) value=float(int32_t(u16(p))-((p[1]&0x80)?65536:0))/32768.f;
        else {
            const uint32_t v=uint32_t(p[0]) | uint32_t(p[1])<<8 | uint32_t(p[2])<<16;
            value=float(int32_t(v)-((v&0x800000)?16777216:0))/8388608.f;
        }
        if (!std::isfinite(value) || value < -1.f || value > 1.f) return fail("WAV contains non-finite or overloaded samples; no automatic normalization applied");
        a.pcm[i]=value;
    }
    out=std::move(a); error.clear(); return true;
}
bool load(const std::string& path, Audio& out, std::string& error) {
    const auto native=eiii::shim::path_from_utf8(path);
    if(native.empty()){error="Invalid UTF-8 WAV path";return false;}
    std::ifstream file(native,std::ios::binary|std::ios::ate);
    if (!file) { error="Cannot open WAV file"; return false; }
    const auto n=file.tellg();
    if (n<12 || uint64_t(n)>max_file_bytes) { error="WAV file size out of bounds"; return false; }
    std::vector<uint8_t> bytes(static_cast<size_t>(n));
    file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(bytes.data()),std::streamsize(bytes.size()))) { error="WAV read failed"; return false; }
    return decode(bytes,out,error);
}
float Audio::at(double seconds,unsigned channel) const noexcept {
    if (!std::isfinite(seconds) || seconds<0 || pcm.empty() || !channels) return 0;
    const double pos=seconds*rate;
    if (pos>=double(frames())) return 0;
    const size_t i=size_t(pos), j=std::min(i+1,frames()-1);
    const unsigned c=std::min(channel,channels-1);
    const double f=pos-double(i);
    return float(double(pcm[i*channels+c])*(1-f)+double(pcm[j*channels+c])*f);
}
float Audio::filtered_at(double seconds,unsigned channel,double adc_rate) const noexcept {
    if(!std::isfinite(seconds)||seconds<0||!std::isfinite(adc_rate)||adc_rate<=0||!frames())return 0;
    const double pos=seconds*rate;
    if(pos>=double(frames()))return 0;
    // For almost-equal source/ADC rates, a 64-tap sinc can create
    // intersample peaks above full scale even from valid PCM16 input.
    // The EIII ADC path must not clip them. Linear interpolation is
    // convex (cannot exceed either input sample) and changes only the
    // host WAV adapter; the firmware and emulated hardware stay intact.
    // Keep the band-limited path for material rate conversion.
    if(adc_rate >= double(rate)*.998 && adc_rate <= double(rate)*1.002)
        return at(seconds,channel);
    const double cutoff=.47*std::min(1.,adc_rate/double(rate));
    constexpr int radius=32;
    constexpr double pi=3.14159265358979323846;
    const int64_t centre=int64_t(pos);
    const unsigned c=std::min(channel,channels-1);
    double sum=0,weight=0;
    for(int k=-radius+1;k<=radius;++k) {
        const int64_t i=centre+k;const double x=double(i)-pos;
        const double z=2*pi*cutoff*x;
        const double sinc=std::abs(z)<1e-12?1.:std::sin(z)/z;
        const double window=.5+.5*std::cos(pi*x/radius);
        const double w=2*cutoff*sinc*window;weight+=w;
        if(i>=0&&uint64_t(i)<frames())sum+=double(pcm[size_t(i)*channels+c])*w;
    }
    return float(weight?sum/weight:0);
}
bool verify_payload(std::span<const uint16_t> recorded,
                    std::span<const int16_t> left,std::span<const int16_t> right,
                    CaptureWindow timing,CaptureLayout* measured) {
    if(measured)*measured={};
    // No sample-header/channel boundary information is invented here:
    // equal planar blocks are the actual layout measured on the single-sample
    // firmware capture. The caller must separately enforce single-sample RAM.
    if(left.empty()||left.size()!=right.size()||(recorded.size()&1u))return false;
    const size_t channel_words=recorded.size()/2;
    if(channel_words<left.size())return false;
    const size_t extra=channel_words-left.size();
    const size_t allowed_post=timing.adc_after+
        std::min(timing.zero_guard,size_t(2)); // two trailing zero guard words OBSERVED in v8, not a hardware specification
    const size_t first=extra>allowed_post?extra-allowed_post:0;
    const size_t last=std::min(extra,timing.adc_before);
    if(first>last)return false;
    const auto equal=[](uint16_t a,int16_t b){return int16_t(a)==b;};
    const auto zero=[](uint16_t a){return a==0;};
    size_t matches=0;
    CaptureLayout candidate{};
    for(size_t pre=first;pre<=last;++pre) {
        const size_t post=extra-pre;
        if(post>allowed_post)continue;
        bool valid=true;
        for(size_t c=0;c<2 && valid;++c) {
            const auto chunk=recorded.subspan(c*channel_words,channel_words);
            const auto expected=c?right:left;
            if(!std::all_of(chunk.begin(),chunk.begin()+std::ptrdiff_t(pre),zero)||
               !std::equal(expected.begin(),expected.end(),
                           chunk.begin()+std::ptrdiff_t(pre),
                           [&](int16_t a,uint16_t b){return equal(b,a);})||
               !std::all_of(chunk.end()-std::ptrdiff_t(post),chunk.end(),zero))
                valid=false;
        }
        if(valid) {
            ++matches;
            candidate={channel_words,pre,post};
            // All-zero and otherwise ambiguous waveform alignment is not
            // proof that the WAV onset survived threshold-triggered sampling.
            if(matches>1)return false;
        }
    }
    if(matches!=1)return false;
    if(measured)*measured=candidate;
    return true;
}

bool verify_forced_payload(std::span<const uint16_t> recorded,
                           std::span<const int16_t> left,std::span<const int16_t> right,
                           size_t adc_before,size_t adc_after,CaptureLayout* measured) {
    if(measured)*measured={};
    if(!adc_before||left.empty()||left.size()!=right.size()||(recorded.size()&1u))return false;
    const size_t words=recorded.size()/2;
    // Bound additions by the actual allocation before forming guard counts.
    if(adc_before>=words||adc_after>words||words-adc_after<2)return false;
    const size_t pre=adc_before+1; // two guards minus the discarded initial read
    const size_t post=adc_after+2;
    if(pre>words||post>words-pre||left.size()!=words-pre-post)return false;
    // The exact total above forces first==last==pre in the existing strict
    // comparator. Every source value and all surrounding zeros must match.
    return verify_payload(recorded,left,right,{pre,adc_after,2},measured);
}

CaptureDiagnostic diagnose_payload(std::span<const uint16_t> recorded,
                                   std::span<const int16_t> left,
                                   std::span<const int16_t> right) {
    CaptureDiagnostic out;
    out.summary="PCM diagnostic: recorded_words="+std::to_string(recorded.size());
    if(recorded.size()&1u) {
        out.summary+="; odd word count, equal planar split unavailable";
        return out;
    }
    out.equal_halves=true;
    // Unlike n+(recorded.size()-2*n)/2, this cannot underflow when the
    // recording is shorter than the reference (165954 words vs 2*83029).
    out.assumed_channel_words=recorded.size()/2;
    out.summary+="; assumed_equal_planar_channel_words="+
        std::to_string(out.assumed_channel_words);
    for(unsigned c=0;c<2;++c) {
        const auto actual=recorded.subspan(c*out.assumed_channel_words,
                                           out.assumed_channel_words);
        const auto expected=c?right:left;
        const size_t common=std::min(actual.size(),expected.size());
        size_t prefix=0,suffix=0;
        while(prefix<common && int16_t(actual[prefix])==expected[prefix])++prefix;
        while(suffix<common &&
              int16_t(actual[actual.size()-1-suffix])==expected[expected.size()-1-suffix])++suffix;
        out.summary+=c?"; R":"; L";
        out.summary+=" source_words="+std::to_string(expected.size());
        if(actual.size()<expected.size())
            out.summary+=" deficit="+std::to_string(expected.size()-actual.size());
        else out.summary+=" extra="+std::to_string(actual.size()-expected.size());
        out.summary+=" equal_prefix="+std::to_string(prefix)+
                     " equal_suffix="+std::to_string(suffix);
        if(prefix<common) {
            out.summary+=" first_difference="+std::to_string(prefix)+
                         " expected="+std::to_string(expected[prefix])+
                         " recorded="+std::to_string(int16_t(actual[prefix]));
        } else if(actual.size()!=expected.size())
            out.summary+=" first_difference="+std::to_string(common)+" (length)";
        else out.summary+=" first_difference=none";
    }
    return out;
}

}
