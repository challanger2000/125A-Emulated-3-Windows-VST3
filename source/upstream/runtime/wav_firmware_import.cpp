#include "wav_firmware_import.h"
#include "wav_place_key.h"
#include "eiii_runtime_impl.h"
#include "adc_quantize.h"
#include "sample_input.h"
#include "panel/eiii_panel_model.h"
#include "standalone/first_start_midi.h"
#include "standalone/ui_macro.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <regex>
#include <stdexcept>

namespace eiii::wav {
namespace {
// Diagnostic-only ADC event history, opt-in via EIII_WAV_FULL_TRACE.
// Never touched by the VST3 audio callback.
struct AdcEvent { double time; int16_t left, right; uint8_t ctrl, phase; };
struct Driver {
    runtime& rt;
    const std::atomic<bool>& cancel;
    double rate;
    const Audio* source=nullptr;
    double source_start=-1,source_duration=0;
    uint64_t source_start_cycle=0;
    bool input_overload=false;
    std::vector<std::string> observed;
    std::chrono::steady_clock::time_point next_heartbeat=std::chrono::steady_clock::now()+std::chrono::seconds(15);
    std::vector<int16_t> supplied[2];
    uint64_t adc_before=0,adc_after=0,adc_during=0;
    uint64_t adc_last_cycle=0,adc_cycle_gaps=0,adc_cycle_backwards=0;
    double adc_during_first=-1,adc_during_last=-1;
    bool trace_adc=false,trace_truncated=false;
    std::vector<AdcEvent> adc_timeline;
    double adc_first_time=-1,adc_last_time=-1;
    Driver(runtime& r,const std::atomic<bool>& c,double sr):rt(r),cancel(c),rate(sr) {}
    std::string screen() {
        panel_snapshot s{};rt.snapshot(s);
        return firmware_menu_lcd(s.lcd);
    }
    void advance(double seconds=.001) {
        // Keep the deadline in whole CPU cycles. Floating-point seconds can
        // round above the already reached cycle and request it forever.
        const uint64_t start=rt.cycles();
        const uint64_t span=std::max<uint64_t>(1, uint64_t(std::ceil(seconds*runtime::cpu_hz)));
        const uint64_t deadline=start+span;
        uint64_t last_cycles=start;
        unsigned stalled_steps=0;
        while(rt.cycles()<deadline) {
            if(cancel.load())throw std::runtime_error("WAV import cancelled; previous instrument retained");
            if(std::chrono::steady_clock::now()>=next_heartbeat) {
                const auto lcd=screen();
                std::fprintf(stderr,"WAV FIRMWARE HEARTBEAT: guest_seconds=%.3f LCD=%s\\n",rt.seconds(),lcd.c_str());
                std::fflush(stderr);
                next_heartbeat=std::chrono::steady_clock::now()+std::chrono::seconds(15);
            }
            if(rt.stop_code())throw std::runtime_error("Emulator stopped during WAV import");
            const uint64_t target=std::min(deadline,rt.cycles()+uint64_t(10000));
            rt.run_until_cycle(target);
            const uint64_t now_cycles=rt.cycles();
            if(now_cycles==last_cycles) {
                if(++stalled_steps>=1000)
                    throw std::runtime_error("WAV sampling emulator clock stalled at cycle "+
                        std::to_string(now_cycles)+" target="+std::to_string(target)+
                        " LCD: "+screen());
            } else {last_cycles=now_cycles;stalled_steps=0;}
            rt.render_audio();rt.audio().clear();
            const auto current=screen();
            if(observed.empty() || observed.back()!=current) {
                if(observed.size()==1024)observed.erase(observed.begin());
                observed.push_back(current);
            }
        }
    }
    void wait(const std::string& text,double timeout=10) {
        const double end=rt.seconds()+timeout;
        while(screen().find(text)==std::string::npos && std::none_of(observed.begin(),observed.end(),[&](const auto& item){return item.find(text)!=std::string::npos;})) {
            if(rt.seconds()>end) {
                // Preserve the actual last firmware screens for reproducible
                // field diagnosis; never guess how the EIII menu should exit.
                std::string history;
                const size_t begin=observed.size()>8?observed.size()-8:0;
                for(size_t i=begin;i<observed.size();++i) {
                    history+=" [screen "+std::to_string(i)+": "+observed[i]+"]";
                }
                std::fprintf(stderr,"WAV FIRMWARE TIMEOUT: expected=%s current_LCD=%s history=%s\\n",
                             text.c_str(),screen().c_str(),history.c_str());
                std::fflush(stderr);
                throw std::runtime_error("Firmware timeout waiting for "+text+
                    "; LCD: "+screen()+"; recent LCD:"+history);
            }
            advance();
        }
    }
    void main() {
        const double end=rt.seconds()+120;
        while(!std::regex_search(screen(),std::regex("^P[0-9][0-9] "))) {
            if(rt.seconds()>end)throw std::runtime_error("EIII did not reach main screen: "+screen());
            advance();
        }
    }
    void press(const std::string& id) {
        observed.clear();
        const int n=panel_find_control(id.c_str());
        if(n<0)throw std::runtime_error("Unknown firmware control "+id);
        const auto& k=panel_controls[n];
        ui_macro sequence;
        sequence.panel=[&](const std::string&,bool down) {
            if(k.route==panel_route::scan)rt.panel_switch(k.code,down);
            else if(k.route==panel_route::xsw)rt.extra_switch(k.code,down);
        };
        sequence.extra=sequence.panel;
        sequence.press(id);
        try {
            while(!sequence.done()) {sequence.tick(rt.seconds());advance();}
        } catch(...) {
            // Cancellation during the 80 ms hold must never strand ENTER or a
            // scanner key in the original (rollback) instrument.
            if(k.route==panel_route::scan)rt.panel_switch(k.code,false);
            else if(k.route==panel_route::xsw)rt.extra_switch(k.code,false);
            throw;
        }
        advance(.25);
    }
    void number(unsigned value) {
        press("keypad_"+std::to_string(value/10));
        press("keypad_"+std::to_string(value%10));
    }
    void key(unsigned requested,PlaceKeyStage stage) {
        // EIII physical DATA slider is ADC multiplexer input X3.
        // EIII names MIDI middle C (60) C3, not host-style C4.
        // Select key via the measured LCD/ADC pitch, never guessed offsets.
        const char* prompt=stage==PlaceKeyStage::original?"Select Original Key":
                           stage==PlaceKeyStage::low?"Select Low Key":"Select High Key";
        wait(prompt);
        unsigned current=0;
        const std::string before=screen();
        if(!parse_place_key(before,stage,current))
            throw std::runtime_error("Cannot parse EIII key field for "+std::string(prompt)+
                " LCD: "+before);
        if(current==requested)return;
        const bool diagnostic=std::getenv("EIII_WAV_FULL_TRACE")!=nullptr;
        if(diagnostic) {
            std::fprintf(stderr,"WAV KEY STAGE BEGIN: %s current=%u target=%u ADC_X3=%04X read_count=%llu guest_pc=%06X LCD: %s\n",
                prompt,current,requested,unsigned(rt.adc_input(3)),
                (unsigned long long)rt.internals().m.adc_pot_reads[3],
                unsigned(rt.internals().m.cpu->state_pc()),before.c_str());
            std::fflush(stderr);
        }

        const auto read_at=[&](uint16_t value,unsigned& found)->bool {
            if(diagnostic)rt.internals().m.adc_x3_diag_budget=24;
            const uint64_t reads_before=rt.internals().m.adc_pot_reads[3];
            rt.set_adc_input(3,value); // same guest-facing control as DATA slider
            advance(.22); // first observation; the LCD may still be updating
            const auto lcd=screen();
            if(diagnostic) {
                std::fprintf(stderr,"WAV KEY ADC PROBE: %s value=%04X ADC_reads=%llu pc=%06X LCD: %s\n",
                    prompt,unsigned(value),
                    (unsigned long long)(rt.internals().m.adc_pot_reads[3]-reads_before),
                    unsigned(rt.internals().m.cpu->state_pc()),lcd.c_str());
                std::fflush(stderr);
            }
            if(!parse_place_key(lcd,stage,found)) {
                std::fprintf(stderr,"WAV KEY: invalid LCD after DATA adc=0x%04x: %s\\n",
                             unsigned(value),lcd.c_str());
                // Diagnostic-only: observe the SAME ADC position over a
                // bounded guest-time window. No subsequent note is accepted,
                // no guest RAM is patched, and no bank can be committed.
                if(diagnostic) {
                    for(unsigned k=0;k<15;++k) {
                        rt.internals().m.adc_x3_diag_budget=(k<2?8:0);
                        advance(.10);
                        const std::string now=screen();
                        unsigned later=0;
                        const bool valid=parse_place_key(now,stage,later);
                        std::fprintf(stderr,"WAV KEY SETTLE t=%.3f step=%u valid=%u value=%u ADC3_reads=%llu pc=%06X LCD: %s\n",
                            rt.seconds(),k,unsigned(valid),later,
                            (unsigned long long)rt.internals().m.adc_pot_reads[3],
                            unsigned(rt.internals().m.cpu->state_pc()),now.c_str());
                    }
                    // Print raw guest DDRAM bytes (not the sanitized GUI text).
                    for(unsigned row=0;row<2;++row) {
                        std::fprintf(stderr,"WAV LCD DDRAM row=%u:",row);
                        for(unsigned col=0;col<40;++col)
                            std::fprintf(stderr," %02X",unsigned(rt.internals().m.lcd_ddram[row*64+col]));
                        std::fprintf(stderr,"\n");
                    }
                }
                std::fflush(stderr);
                return false;
            }
            std::fprintf(stderr,"WAV KEY: %s DATA adc=0x%04x -> MIDI=%u (target=%u)\\n",
                         prompt,unsigned(value),found,requested);
            std::fflush(stderr);
            return true;
        };
        KeySeekResult result{};
        std::string reason;
        const uint16_t start_adc=rt.adc_input(3);
        // Explicitly calibrate THIS firmware dialog with observed ADC
        // movements. The displayed initial key may be an OS default
        // unrelated to the physical slider position (v14 root cause).
        if(!seek_key_measured(requested,read_at,result,reason))
            throw std::runtime_error("EIII DATA slider selection failed for "+std::string(prompt)+
                " requested="+std::to_string(requested)+
                " default_note="+std::to_string(current)+
                " default_ADC="+std::to_string(start_adc)+
                " last_note="+std::to_string(result.note)+
                " last_ADC="+std::to_string(result.adc)+
                " probes="+std::to_string(result.probes)+
                ": "+reason+" LCD: "+screen());
        if(result.note!=requested||rt.adc_input(3)!=result.adc)
            throw std::runtime_error("EIII DATA slider selection state inconsistent");
        std::fprintf(stderr,"WAV KEY VERIFIED: %s requested=%u ADC=0x%04x probes=%u\n",
                     prompt,requested,unsigned(result.adc),result.probes);
        std::fflush(stderr);
    }
    void input(const Audio& a) {
        // Install before FORCE. Its first conversion is discarded by OS 2.42;
        // anchor WAV time zero to the following (first stored) conversion.
        // Threshold arming can lose quiet prefixes beyond its 256-word pre-roll.
        source=&a;source_duration=a.duration();source_start=-1;source_start_cycle=0;
        trace_adc=std::getenv("EIII_WAV_FULL_TRACE")!=nullptr;
        if(trace_adc)adc_timeline.reserve(std::min<size_t>(
            size_t(std::ceil((a.duration()+.5)*44054)),2200000u));
        supplied[0].reserve(size_t(std::ceil((a.duration()+.1)*44100)));
        supplied[1].reserve(supplied[0].capacity());
        rt.set_adc_source([this](double t,int16_t lr[2]) {
            const uint64_t cycle=uint64_t(std::llround(t*runtime::cpu_hz));
            const uint64_t period=(rt.internals().m.adc_ctrl&0x10)?227u:300u;
            if(source_start<0) {
                source_start_cycle=cycle+period;
                source_start=double(source_start_cycle)/runtime::cpu_hz;
            }
            const double s=cycle<source_start_cycle?-1:
                double(cycle-source_start_cycle)/runtime::cpu_hz;
            const double adc_rate=10000000./((rt.internals().m.adc_ctrl&0x10)?227.:300.);
            float file_l=source->filtered_at(s,0,adc_rate),file_r=source->filtered_at(s,1,adc_rate);
            if(std::abs(file_l)>1.f||std::abs(file_r)>1.f)input_overload=true;
            const float* buffers[]={&file_l,&file_r};float left=0,right=0;
            sampling::SampleInput input;input.channels=source->channels;input.data32=buffers;
            sampling::copy_sample_input(input,&left,&right,1);
            lr[0]=adc::quantize(left,1.);lr[1]=adc::quantize(right,1.);
            if(trace_adc) {
                if(adc_timeline.size()<2200000u)
                    adc_timeline.push_back({t,lr[0],lr[1],
                        uint8_t(rt.internals().m.adc_ctrl),
                        uint8_t(s<0?0:(s>=source->duration()?2:1))});
                else trace_truncated=true;
            }
            if(adc_last_cycle) {
                if(cycle<=adc_last_cycle)++adc_cycle_backwards;
                else if(cycle-adc_last_cycle!=period)++adc_cycle_gaps;
            }
            adc_last_cycle=cycle;
            if(adc_first_time<0)adc_first_time=t;
            adc_last_time=t;
            if(s<0)++adc_before;
            else if(s>=source->duration())++adc_after;
            else {
                ++adc_during;
                if(adc_during_first<0)adc_during_first=t;
                adc_during_last=t;
            }
            if(s>=0 && s<source->duration()) {
                supplied[0].push_back(lr[0]);supplied[1].push_back(lr[1]);
            }
        });
    }
    void no_input() {rt.set_adc_source({});source=nullptr;}
};
uint32_t read32(const uint8_t* p) {return uint32_t(p[0])|uint32_t(p[1])<<8|uint32_t(p[2])<<16|uint32_t(p[3])<<24;}
std::array<bool,100> banks(runtime& rt) {
    auto* hd=rt.internals().hd_image(1);
    std::array<uint8_t,512> block{};
    if(!hd || !hd->read(0,block.data()) || std::memcmp(block.data(),"EMU3",4))
        throw std::runtime_error("D1 must have a readable EMU3 directory");
    const uint32_t lba=read32(block.data()+0x10),n=read32(block.data()+0x14);
    if(!n || n>4096 || uint64_t(lba)+n>hd->length()/512)throw std::runtime_error("Invalid D1 directory bounds");
    std::array<bool,100> used{};
    for(uint32_t k=0;k<n;++k) {
        if(!hd->read(lba+k,block.data()))throw std::runtime_error("D1 directory read failed");
        for(size_t p=0;p<512;p+=32)
            if(block[p+0x1a]==0x81 && block[p+0x11]<100)used[block[p+0x11]]=true;
    }
    return used;
}
std::string bank_label(int n) {return "B"+std::string(n<10?"0":"")+std::to_string(n)+" ";}
void save(Driver& d,int bank) {
    if(banks(d.rt)[size_t(bank)])throw std::runtime_error("Refusing to overwrite existing bank");
    const auto serial=d.rt.hd_writes(1);
    d.press("save_bank");d.wait("SAVE BANK into");d.wait("D1 ");
    d.number(unsigned(bank));d.wait(bank_label(bank));
    d.press("enter");d.wait("Saving Bank");d.main();
    if(d.rt.hd_writes(1)==serial || !banks(d.rt)[size_t(bank)])throw std::runtime_error("Firmware bank save was not confirmed on D1");
}
void load_bank(Driver& d,int bank,unsigned preset) {
    d.main();d.press("load_bank");d.wait("LOAD BANK from");d.wait("Select a Bank",30);
    d.number(unsigned(bank));d.wait(bank_label(bank));d.press("enter");d.wait("Loading Bank");d.main();
    d.number(preset);d.wait("P"+std::string(preset<10?"0":"")+std::to_string(preset)+" ");
}
void verify_pcm(runtime& rt,const Driver& d) {
    const uint32_t ptr=rt.debug_read32(0x1a1b4+4);
    const uint32_t top=rt.debug_read32(0x1a344);
    if(ptr<0x400000 || ptr>0xbfd800-0x5c || top>0xbfd800 || top<=ptr+0x5c || (ptr&1u) || (top&1u))
        throw std::runtime_error("Firmware did not allocate a valid sample");
    std::vector<uint16_t> ram,mem;
    if(!rt.copy_volatile_memory(ram,mem))throw std::runtime_error("Cannot verify recorded sample");
    if(d.input_overload)throw std::runtime_error("WAV reconstruction overload; no normalization or clipped import committed");
    if(d.adc_cycle_gaps || d.adc_cycle_backwards)
        throw std::runtime_error("WAV ADC conversions missed or repeated; import refused");
    const double adc_rate=runtime::cpu_hz/227.;
    const size_t min_cycles=size_t(std::floor(d.source_duration*adc_rate));
    const size_t max_cycles=size_t(std::ceil(d.source_duration*adc_rate));
    if(d.adc_during<min_cycles||d.adc_during>max_cycles||
       d.supplied[0].size()!=d.adc_during||d.supplied[1].size()!=d.adc_during||
       d.adc_during_first<d.source_start-1e-8 ||
       d.adc_during_first>d.source_start+1./adc_rate+1e-8 ||
       d.adc_during_last<d.source_start+d.source_duration-1./adc_rate-1e-8)
        throw std::runtime_error("WAV ADC source timing incomplete; cannot prove whole-file capture");
    // The isolated import bank must contain exactly S01. Only under this
    // invariant does the global allocator top bound this single sample.
    if(ptr!=0x400000)throw std::runtime_error("WAV S01 is not the only allocated sample");
    for(unsigned n=2;n<100;++n)if(rt.debug_read32(0x1a1b4+4*n))
        throw std::runtime_error("WAV sample layout not isolated; global top cannot bound S01");
    if(rt.debug_read32(ptr+0x34)!=44053 || (rt.debug_read16(ptr+0x3a)&0x60)!=0x60)
        throw std::runtime_error("WAV firmware rate/stereo sample flags are unexpected");
    const size_t start=(ptr+0x5c-0x400000)/2,end=(top-0x400000)/2;
    std::fprintf(stderr,"WAV PCM FIRMWARE: sample_ptr=0x%08x global_top=0x%08x rate=0x%08x flags=0x%04x\\n",ptr,top,rt.debug_read32(ptr+0x34),rt.debug_read16(ptr+0x3a));
    std::fprintf(stderr,"WAV ADC TRACE: before=%llu during=%llu after=%llu first_time=%.9f last_time=%.9f source_start=%.9f\\n",(unsigned long long)d.adc_before,(unsigned long long)d.adc_during,(unsigned long long)d.adc_after,d.adc_first_time,d.adc_last_time,d.source_start);
    std::fflush(stderr);
    if(start>end || end>mem.size())throw std::runtime_error("Firmware sample bounds invalid");
    const auto payload=std::span<const uint16_t>(mem).subspan(start,end-start);
    // OS 2.42 stores equal planar blocks. Verify the firmware's raw and
    // playable bounds, including its two-word guards, before using the split.
    if(payload.size()&1u)throw std::runtime_error("WAV firmware stereo extent is odd");
    const uint32_t right_offset=0x5c+uint32_t(payload.size());
    const uint32_t total_bytes=top-ptr;
    if(rt.debug_read32(ptr+0x14)!=0x5c ||
       rt.debug_read32(ptr+0x18)!=right_offset ||
       rt.debug_read32(ptr+0x1c)!=right_offset-2 ||
       rt.debug_read32(ptr+0x20)!=total_bytes-2 ||
       rt.debug_read32(ptr+0x24)!=0x60 ||
       rt.debug_read32(ptr+0x28)!=right_offset+4 ||
       rt.debug_read32(ptr+0x2c)!=right_offset-6 ||
       rt.debug_read32(ptr+0x30)!=total_bytes-6)
        throw std::runtime_error("WAV firmware stereo header/guard bounds differ from OS 2.42 FORCE layout");
    CaptureLayout layout{};
    const bool exact=verify_forced_payload(payload,d.supplied[0],d.supplied[1],
                                           size_t(d.adc_before),size_t(d.adc_after),&layout);
    if(exact) {
        std::fprintf(stderr,"WAV PCM VERIFIED: channel_words=%zu leading_silence=%zu trailing_silence=%zu supplied=%zu ADC_before=%llu ADC_after=%llu\\n",
                     layout.channel_words,layout.pre,layout.post,d.supplied[0].size(),
                     (unsigned long long)d.adc_before,(unsigned long long)d.adc_after);
        std::fflush(stderr);
    }
    if(!exact) {
        const auto diagnostic=diagnose_payload(payload,d.supplied[0],d.supplied[1]);
        std::fprintf(stderr,"%s\n",diagnostic.summary.c_str());
        // Inspect actual memory without assuming that the extra words are zero.
        // Report first exact mismatch and a small neighborhood of each channel
        // boundary; never accept corrupted data based on this diagnosis.
        const size_t n=d.supplied[0].size();
        size_t first=n;
        for(size_t i=0;i<std::min(n,payload.size());++i)
            if(int16_t(payload[i])!=d.supplied[0][i]){first=i;break;}
        std::fprintf(stderr,"WAV PCM MISMATCH: words=%zu supplied_per_channel=%zu first_left_mismatch=%zu\\n",payload.size(),n,first);
        const auto dump=[&](const char* label,size_t center) {
            std::fprintf(stderr,"%s center=%zu:",label,center);
            const size_t lo=center>5?center-5:0;
            for(size_t i=lo;i<std::min(payload.size(),center+6);++i)
                std::fprintf(stderr," [%zu]=%d",i,int16_t(payload[i]));
            std::fprintf(stderr,"\\n");
        };
        dump("LEFT-END",n);
        if(diagnostic.equal_halves)
            dump("ASSUMED-RIGHT-BEGIN",diagnostic.assumed_channel_words);
        dump("RIGHT-END",payload.size());
        // Locate matching right-channel starts without assuming an equal split.
        // Diagnostic only; the strict verifier still decides acceptance.
        size_t matches=0;
        if(!d.supplied[1].empty() && payload.size()>=d.supplied[1].size()) {
            for(size_t off=0;off+d.supplied[1].size()<=payload.size();++off) {
                if(int16_t(payload[off])!=d.supplied[1][0])continue;
                if(std::equal(d.supplied[1].begin(),d.supplied[1].end(),payload.begin()+std::ptrdiff_t(off),
                    [](int16_t a,uint16_t b){return a==int16_t(b);})) {
                    if(matches<8)std::fprintf(stderr,"WAV PCM RIGHT MATCH: offset=%zu\\n",off);
                    ++matches;
                }
            }
        }
        std::fprintf(stderr,"WAV PCM RIGHT MATCHES: %zu\\n",matches);
        // Exact 64-word fingerprints identify the sample boundaries without
        // presupposing a stereo layout or interpreting global RAM top as length.
        const size_t probe=std::min<size_t>(64,d.supplied[0].size());
        if(probe && payload.size()>=probe)for(unsigned channel=0;channel<2;++channel) {
            const auto& src=d.supplied[channel];
            auto it=payload.begin();
            size_t count=0;
            while(it!=payload.end()) {
                it=std::search(it,payload.end(),src.begin(),src.begin()+std::ptrdiff_t(probe),
                    [](uint16_t memory,int16_t expected){return int16_t(memory)==expected;});
                if(it==payload.end())break;
                const size_t offset=size_t(it-payload.begin());
                if(count<8)std::fprintf(stderr,"WAV PCM PREFIX channel=%u RAM_offset=%zu\\n",channel,offset);
                ++count;++it;
            }
            std::fprintf(stderr,"WAV PCM PREFIX MATCHES channel=%u count=%zu\\n",channel,count);
        }
        if(d.trace_adc) {
            const auto& events=d.adc_timeline;
            std::fprintf(stderr,"WAV ADC FULL timeline_events=%zu truncated=%u before=%llu during=%llu after=%llu\\n",
                events.size(),unsigned(d.trace_truncated),
                (unsigned long long)d.adc_before,(unsigned long long)d.adc_during,
                (unsigned long long)d.adc_after);
            size_t nonmonotonic=0,spaced=0;
            for(size_t i=1;i<events.size();++i) {
                const auto& prev=events[i-1];const auto& curr=events[i];
                if(curr.time<=prev.time)++nonmonotonic;
                const uint64_t previous_cycle=uint64_t(std::llround(prev.time*runtime::cpu_hz));
                const uint64_t current_cycle=uint64_t(std::llround(curr.time*runtime::cpu_hz));
                const uint64_t period=(curr.ctrl&0x10)?227:300;
                if(curr.ctrl==prev.ctrl && current_cycle>previous_cycle+period)++spaced;
            }
            std::fprintf(stderr,"WAV ADC EVENT TIMING nonmonotonic=%zu intervals_larger_than_period=%zu (reads need not be continuous)\\n",
                nonmonotonic,spaced);
            const size_t adc_probe=std::min<size_t>(64,std::min(events.size(),payload.size()));
            size_t adc_matches=0;
            if(adc_probe)for(size_t off=0;off+adc_probe<=events.size();++off) {
                if(int16_t(payload[0])!=events[off].left)continue;
                size_t n=1;
                while(n<adc_probe && int16_t(payload[n])==events[off+n].left)++n;
                if(n==adc_probe) {
                    if(adc_matches<8)std::fprintf(stderr,
                        "WAV ADC LEFT ALIGN event=%zu t=%.9f phase=%u source_offset_ms=%.3f\\n",
                        off,events[off].time,unsigned(events[off].phase),
                        1000.*(events[off].time-d.source_start));
                    ++adc_matches;
                }
            }
            std::fprintf(stderr,"WAV ADC LEFT ALIGN MATCHES=%zu\\n",adc_matches);
            const size_t preview=std::min<size_t>(8,events.size());
            for(size_t k=0;k<preview;++k) {
                const auto& e=events[k];
                std::fprintf(stderr,"WAV ADC EVENT[%zu] t=%.9f phase=%u ctrl=0x%02x left=%d right=%d\\n",
                    k,e.time,unsigned(e.phase),unsigned(e.ctrl),int(e.left),int(e.right));
            }
        }
        std::fflush(stderr);
        throw std::runtime_error("Recorded sample length/PCM differs from complete WAV ADC stream: recorded words="+
            std::to_string(end-start)+", supplied per channel="+std::to_string(d.supplied[0].size())+
            "; previous instrument retained; "+diagnostic.summary+
            "; ADC_before="+std::to_string(d.adc_before)+
            " ADC_during="+std::to_string(d.adc_during)+
            " ADC_after="+std::to_string(d.adc_after)+
            " source_start_cycle="+std::to_string(std::llround(d.source_start*runtime::cpu_hz))+
            " source_first_ADC_cycle="+std::to_string(std::llround(d.adc_during_first*runtime::cpu_hz))+
            " source_last_ADC_cycle="+std::to_string(std::llround(d.adc_during_last*runtime::cpu_hz)));
    }
}
}
bool execute(runtime& original,const runtime_config& config,const Audio& audio,
             const Options& opt,const std::atomic<bool>& cancel,Result& out,std::string& error) {
    try {
        if(!opt.valid() || !fits(audio))throw std::runtime_error("Invalid WAV range or sample capacity");
        std::fprintf(stderr,"WAV FIRMWARE STAGE: begin; duration=%.2f s\n",audio.duration());std::fflush(stderr);
        Driver previous(original,cancel,config.sample_rate);
        // Stop at a known UI state before creating the safety bank. No user sample,
        // preset or bank slot is overwritten. Candidate work uses a new runtime.
        if(!std::regex_search(previous.screen(),std::regex("^P[0-9][0-9] ")))
            throw std::runtime_error("Return EIII to its main screen before WAV import");
        auto used=banks(original);int backup=-1,bank=-1;
        for(int n=99;n>=0;--n)if(!used[size_t(n)]) {if(backup<0)backup=n;else {bank=n;break;}}
        if(bank<0)throw std::runtime_error("D1 needs two unused bank slots for safe WAV import");
        std::fprintf(stderr,"WAV FIRMWARE STAGE: save safety bank B%02d\n",backup);std::fflush(stderr);
        save(previous,backup);
        std::fprintf(stderr,"WAV FIRMWARE STAGE: safety bank saved\n");std::fflush(stderr);
        runtime_config cfg=config;
        for(unsigned id=1;id<8;++id)if(original.drive_kind(id)==scsi_kind::hd)cfg.hd_changes[id]=original.hd_changes(id);
        auto candidate=std::make_unique<runtime>();
        if(!candidate->open(cfg,error)) {original.bind_thread();return false;}
        std::fprintf(stderr,"WAV FIRMWARE STAGE: boot candidate\n");std::fflush(stderr);
        Driver d(*candidate,cancel,cfg.sample_rate);d.main();
        std::fprintf(stderr,"WAV FIRMWARE STAGE: candidate ready\n");std::fflush(stderr);
        // Fresh power-on bank only; firmware still owns all allocations.
        for(unsigned n=1;n<100;++n)if(candidate->debug_read32(0x1a1b4+4*n))
            throw std::runtime_error("Power-on bank contains samples; refusing replacement");
        d.number(opt.preset);d.wait("P"+std::string(opt.preset<10?"0":"")+std::to_string(opt.preset)+" ");
        std::fprintf(stderr,"WAV FIRMWARE STAGE: enter sampling\n");std::fflush(stderr);
        d.press("sample");d.wait("S01 Empty Sample");
        candidate->set_adc_input(0,0x3880); // UI unity setting; no guest-memory write.
        // Original SAMPLE 8 (Force Sampling), not threshold-triggered SAMPLE 7.
        // Anchor the complete WAV to FORCE's first stored ADC conversion.
        // No signal-dependent trigger, invented preamble or PCM trimming.
        d.input(audio);d.press("keypad_8");d.wait("Sampling...");
        const double end=d.source_start+audio.duration();
        while(candidate->seconds()<end)d.advance();
        d.press("enter");d.no_input();
        if(d.input_overload)throw std::runtime_error("WAV reconstruction overload; no normalization or clipped import committed");
        d.wait("S01 Untitled Sample");
        std::fprintf(stderr,"WAV FIRMWARE STAGE: verify captured PCM\n");std::fflush(stderr);
        verify_pcm(*candidate,d);
        // SAMPLE 6 first opens the PLACE SAMPLE submodule. Its initial ENTER
        // opens pri/sec selection (same sequence as tests/sampling.cpp).
        d.press("keypad_6");d.wait("PLACE SAMPLE");d.press("enter");
        d.wait("Select pri/sec");d.press("enter");
        d.wait("Select Original Key");d.key(opt.root,PlaceKeyStage::original);d.press("enter");
        d.wait("Select Low Key");d.key(opt.low,PlaceKeyStage::low);d.press("enter");
        d.wait("Select High Key");d.key(opt.high,PlaceKeyStage::high);d.press("enter");
        // A wider requested range may overlap the firmware's automatic zone.
        // This is the fresh, isolated candidate (only S01), never the user's
        // previous instrument. Confirm the actual original firmware prompt.
        if(d.screen().find("Overwrite Zone? Y/N")!=std::string::npos)d.press("yes");
        d.wait("Select a Submodule");d.press("sample");d.main();
        std::fprintf(stderr,"WAV FIRMWARE STAGE: save imported bank B%02d\n",bank);std::fflush(stderr);
        save(d,bank);cfg.hd_changes[1]=candidate->hd_changes(1);
        // Verify durable data through a fresh firmware boot/load, not just RAM or
        // a successful write call. Failures leave the previous machine intact.
        candidate.reset();auto restored=std::make_unique<runtime>();
        if(!restored->open(cfg,error)){original.bind_thread();return false;}
        std::fprintf(stderr,"WAV FIRMWARE STAGE: reboot and verify bank\n");std::fflush(stderr);
        Driver fresh(*restored,cancel,cfg.sample_rate);fresh.main();load_bank(fresh,bank,opt.preset);
        verify_pcm(*restored,d);
        // Hardware-documented "Preset Definition -> Edit Assignment" is
        // read-only until an edit is committed. In the user's opt-in firmware
        // diagnostic, query this ORIGINAL firmware display, not a fabricated
        // sample-map structure. MIDI on channel 14 matches the tested VST3
        // host UART path (9D). No sample/preset RAM writes or DSP changes.
        if(std::getenv("EIII_WAV_FULL_TRACE")) {
            std::fprintf(stderr,"WAV ZONE INSPECTION: isolated original-firmware EDIT ASSIGNMENT\n");
            std::fflush(stderr);
            try {
                // The diagnostic MUST NOT change the accepted instrument's
                // playback state or its selected preset: use a throwaway
                // runtime with the same saved disk generation.
                auto inspection=std::make_unique<runtime>();
                std::string inspect_error;
                if(!inspection->open(cfg,inspect_error))
                    throw std::runtime_error("Cannot boot read-only preset inspection: "+inspect_error);
                Driver inspect(*inspection,cancel,cfg.sample_rate);
                inspect.main();
                load_bank(inspect,bank,opt.preset);
                inspect.press("preset_definition");
                inspect.wait("Select a Submodule",8);
                inspect.press("keypad_2");
                inspect.wait("EDIT ASSIGNMENT",8);
                std::fprintf(stderr,"WAV ZONE MENU LCD: %s\n",inspect.screen().c_str());
                const uint8_t keys[]={47,48,59,60,71,72,73,74,84};
                for(uint8_t key:keys) {
                    const uint8_t on[]={0x9d,key,100},off[]={0x8d,key,0};
                    inspect.rt.midi_in(on,sizeof on);
                    inspect.advance(.4);
                    const std::string lcd=inspect.screen();
                    std::fprintf(stderr,"WAV ZONE KEY %u ON LCD: %s\n",
                                 unsigned(key),lcd.c_str());
                    inspect.rt.midi_in(off,sizeof off);
                    inspect.advance(.16);
                }
                std::fprintf(stderr,"WAV ZONE INSPECTION: temporary runtime discarded\n");
                std::fprintf(stderr,"WAV ZONE INSPECTION: complete\n");
            } catch(const std::exception& ex) {
                std::fprintf(stderr,"WAV ZONE INSPECTION INCOMPLETE: %s (accepted runtime unchanged)\n",
                             ex.what());
                // Diagnostic cannot turn a correct bank into a success or
                // repair any mapping. The independent WAV host playback test
                // remains the decisive acceptance check.
            }
            std::fflush(stderr);
        }
        if(cancel.load())throw std::runtime_error("WAV import cancelled before commit");
        Result r;r.machine=std::move(restored);r.config=std::move(cfg);r.bank=bank;r.backup=backup;
        out=std::move(r);error.clear();return true;
    } catch(const std::exception& ex) {error=ex.what();original.bind_thread();return false;}
    catch(...) {error="Unexpected WAV import failure; previous instrument retained";original.bind_thread();return false;}
}
}
