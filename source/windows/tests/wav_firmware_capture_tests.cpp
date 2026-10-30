// Opt-in original-firmware regression. Private ROM/image/WAV files stay external.
#include "../../upstream/runtime/wav_firmware_import.h"
#include "../../upstream/runtime/eiii_runtime_impl.h" // read-only voice diagnostics
#include "../../upstream/panel/eiii_lcd.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool ok,const std::string& why) {if(!ok)throw std::runtime_error(why);}
void advance(eiii::runtime& rt,double seconds) {
    const auto end=rt.cycles()+uint64_t(std::ceil(seconds*rt.cpu_hz));
    while(rt.cycles()<end) {
        require(!rt.stop_code(),"firmware stopped");
        const auto before=rt.cycles();
        rt.run_until_cycle(std::min(end,before+10000));rt.render_audio();rt.audio().clear();
        require(rt.cycles()>before,"firmware clock stalled");
    }
}
void boot(eiii::runtime& rt,const eiii::runtime_config& cfg) {
    std::string error;require(rt.open(cfg,error),"open: "+error);
    for(unsigned ms=0;ms<120000;++ms) {
        eiii::panel_snapshot snapshot{};rt.snapshot(snapshot);
        const auto line=eiii::lcd_line_text(snapshot.lcd,0);
        if(line.size()>3&&line[0]=='P'&&line[3]==' ')return;
        advance(rt,.001);
    }
    throw std::runtime_error("main-screen timeout");
}
uint64_t sample_hash(eiii::runtime& rt) {
    std::vector<uint16_t> ram,samples;
    require(rt.copy_volatile_memory(ram,samples),"sample RAM unreadable");
    const uint32_t top=rt.debug_read32(0x1a344);
    require(top>=0x400000&&top<=0xbfd800&&!(top&1u),"invalid allocated sample extent");
    // Save Bank legitimately uses unallocated sample RAM as firmware workspace.
    // Hash allocated samples, not scratch RAM or transient CPU/voice state.
    const size_t words=(top-0x400000)/2;
    uint64_t hash=14695981039346656037ull;
    for(size_t i=0;i<words;++i){hash^=samples.at(i);hash*=1099511628211ull;}
    return hash;
}
eiii::wav::Audio fixture(unsigned rate,unsigned channels,double seconds,float level,bool silence=false) {
    eiii::wav::Audio a;a.rate=rate;a.channels=channels;
    a.pcm.resize(std::max<size_t>(1,size_t(seconds*rate))*channels);
    for(size_t i=0;i<a.frames();++i)for(unsigned c=0;c<channels;++c) {
        // Deliberately different stereo channels and 20 ms quiet onset: SAMPLE 7
        // cannot retain this prefix. Also include exact silence as its own case.
        const double signal=i<rate/50?0:std::sin(6.283185307179586*(c?1500:1000)*i/rate);
        a.pcm[i*channels+c]=silence?0:(a.frames()==1?level:level*float(signal));
    }
    return a;
}
void rejected(eiii::runtime& original,const eiii::runtime_config& cfg,
              const eiii::wav::Audio& audio,eiii::wav::Options options,bool cancelled) {
    const auto hash=sample_hash(original);
    const auto ptr=original.debug_read32(0x1a1b8),top=original.debug_read32(0x1a344);
    std::atomic<bool> cancel=cancelled;eiii::wav::Result result;std::string error;
    require(!eiii::wav::execute(original,cfg,audio,options,cancel,result,error),"bad import accepted");
    require(!error.empty()&&!result.machine,"failure did not retain ownership");
    if(!options.valid())require(error.find("Invalid WAV range")!=std::string::npos,"wrong range rejection: "+error);
    else if(cancelled)require(error.find("cancelled")!=std::string::npos,"wrong cancellation rejection: "+error);
    else require(error.find("overload")!=std::string::npos||error.find("Overload")!=std::string::npos,
                 "overload case failed for another reason: "+error);
    require(hash==sample_hash(original)&&ptr==original.debug_read32(0x1a1b8)&&
            top==original.debug_read32(0x1a344),"failed import changed original sample RAM");
    advance(original,.01);require(!original.stop_code(),"original stopped after failure");
}
void mapping(eiii::runtime& rt,double output_rate) {
    bool valid=true;
    // Original OS 2.42 clamps the divisor at PCs 0x12560/0x1272A (also
    // 0xEC2F/0xED73). Frozen independent expectations, not read from the
    // observed output: 20 MHz / N, with N >= 0xA4. Source ADC N is 454.
    struct Note {unsigned key,divisor;};
    const Note notes[]={{35,0},{36,1816},{60,454},{72,227},{77,170},
                        {78,164},{84,164},{85,0}};
    auto note=[&](unsigned key,unsigned divisor) {
        const uint8_t on[]={0x9d,uint8_t(key),100},off[]={0x8d,uint8_t(key),0};
        advance(rt,3.0);rt.midi_in(on,sizeof on);advance(rt,.05);
        const uint32_t ptr=rt.debug_read32(0x1a1b8);
        const uint32_t starts[]={(ptr-0x400000+rt.debug_read32(ptr+0x14))/2,
                                 (ptr-0x400000+rt.debug_read32(ptr+0x18))/2};
        unsigned sounding=0,left_voices=0,right_voices=0;
        auto& backend=*rt.internals().backend;
        for(unsigned i=0;i<16;++i) {
            const auto& voice=backend.lanes().state(i);
            // A released lane can remain digitally active while its VCA is
            // closed. Count actual newly sounding stereo voices, not tails.
            if(!voice.active||!backend.cv_word(i,3))continue;
            ++sounding;
            if(voice.rate!=divisor)valid=false;
            const auto initial=voice.rf[3]&0x3fffffu;
            left_voices+=initial==starts[0];right_voices+=initial==starts[1];
            std::cout<<"MIDI voice key="<<key<<" lane="<<i<<" divisor="<<voice.rate
                <<" initial_word="<<initial<<std::endl;
        }
        if(divisor){if(sounding!=2||left_voices!=1||right_voices!=1)valid=false;}
        else if(sounding)valid=false;
        std::vector<float> left;
        const auto end=rt.cycles()+uint64_t(.2*rt.cpu_hz);
        while(rt.cycles()<end) {
            rt.run_until_cycle(std::min(end,rt.cycles()+10000));rt.render_audio();
            const auto& audio=rt.audio();
            for(size_t i=0;i+1<audio.size();i+=2)left.push_back(audio[i]);
            rt.audio().clear();
        }
        rt.midi_in(off,sizeof off);advance(rt,.1);
        double peak=0;
        for(size_t i=0;i<left.size();++i) {
            peak=std::max(peak,double(std::abs(left[i])));
        }
        // Pitch measurement uses the mono fixture so either analog output
        // carries the same reference tone. Stereo RAM is checked separately.
        const double expected=divisor?1000.*454/divisor:1000.;
        double strongest=0,hz=0;
        for(int offset=-10;offset<=10;++offset) {
            const double f=expected+offset;double re=0,im=0;
            for(size_t i=0;i<left.size();++i) {
                const double phase=6.283185307179586*f*i/output_rate;
                re+=left[i]*std::cos(phase);im+=left[i]*std::sin(phase);
            }
            const double amplitude=left.empty()?0:2*std::hypot(re,im)/left.size();
            if(amplitude>strongest){strongest=amplitude;hz=f;}
        }
        std::cout<<"MIDI key="<<key<<" peak="<<peak<<" tone_amplitude="<<strongest<<" measured_Hz="<<hz<<std::endl;
        if(divisor&&strongest<=peak*.03)valid=false;
        if(key==84) {
            double re=0,im=0;
            for(size_t i=0;i<left.size();++i) {
                const double phase=6.283185307179586*4000*i/output_rate;
                re+=left[i]*std::cos(phase);im+=left[i]*std::sin(phase);
            }
            const double wrong=left.empty()?0:2*std::hypot(re,im)/left.size();
            if(wrong>=peak*.03)valid=false;
            std::cout<<"MIDI 84 negative 4kHz control: amplitude="<<wrong<<std::endl;
        }
        return std::pair{peak,hz};
    };
    for(const auto& n:notes) {
        const auto [peak,hz]=note(n.key,n.divisor);
        if(!n.divisor){if(peak>=1e-6)valid=false;}
        else {
            const double expected=1000.*454/n.divisor;
            if(peak<=1e-5||std::abs(hz-expected)>=3)valid=false;
        }
    }
    require(valid,"MIDI zone/pitch check failed; inspect the per-note measurements");
}
}
int main() {
    const char* rom=std::getenv("EIII_ROM_DIR"),*hd=std::getenv("EIII_HD_IMAGE");
    if(!rom||!hd){std::cout<<"SKIP: set EIII_ROM_DIR and EIII_HD_IMAGE (original OS 2.42)\n";return 77;}
    try {
        eiii::runtime_config cfg;cfg.rom_dir=rom;cfg.hd_path=hd;
        struct Case {unsigned rate,channels;double seconds;float level;bool silence;};
        const Case cases[]={
            {44100,2,1.884761904761905,.975f,false},
            {44100,1,1./44100,.5f,false},
            {22050,1,.125,.02f,false},
            {48000,2,.35,.5f,false},
            {96000,1,1.3,.9f,false},
            {192000,2,.2,.5f,false},
            {44100,2,.3,0,true},
            {44100,2,4.0,.5f,false}
        };
        auto original=std::make_unique<eiii::runtime>();boot(*original,cfg);
        const auto baseline_hash=sample_hash(*original);
        size_t passed=0,failed=0;
        auto run=[&](const eiii::wav::Audio& audio,const char* name,bool check_mapping=false) {
            std::atomic<bool> cancel=false;eiii::wav::Result result;std::string error;
            const bool ok=eiii::wav::execute(*original,cfg,audio,{60,36,84,0},cancel,result,error);
            original->bind_thread();require(sample_hash(*original)==baseline_hash,"original PCM changed");
            require(ok,std::string(name)+": "+error);
            require(result.machine&&result.bank!=result.backup,"candidate missing or safety bank reused");
            // execute verifies every supplied ADC word and both channel/guard
            // boundaries twice, including after original-firmware reboot/load.
            original->bind_thread();require(sample_hash(*original)==baseline_hash,"original PCM changed");
            auto& imported=*result.machine;imported.bind_thread();
            if(check_mapping)try{mapping(imported,result.config.sample_rate);}
                catch(const std::exception& ex){++failed;std::cerr<<"FAIL: "<<ex.what()<<std::endl;}
            rejected(imported,result.config,audio,{60,80,20,0},false);
            auto overload=audio;std::fill(overload.pcm.begin(),overload.pcm.end(),1.5f);
            rejected(imported,result.config,overload,{60,36,84,0},false);
            rejected(imported,result.config,audio,{60,36,84,0},true);
            ++passed;std::cout<<"PASS firmware capture/reboot/rollback: "<<name<<" rate="<<audio.rate
                <<" channels="<<audio.channels<<" frames="<<audio.frames()<<std::endl;
            original->bind_thread();
        };
        auto attempt=[&](const eiii::wav::Audio& audio,const char* name,bool check_mapping=false) {
            try {run(audio,name,check_mapping);}
            catch(const std::exception& ex) {
                ++failed;original->bind_thread();
                std::cerr<<"FAIL firmware case rate="<<audio.rate<<" channels="<<audio.channels
                    <<" frames="<<audio.frames()<<": "<<ex.what()<<std::endl;
            }
        };
        if(const char* wav=std::getenv("EIII_REPRO_WAV")) {
            eiii::wav::Audio audio;std::string error;
            require(eiii::wav::load(wav,audio,error),"external WAV: "+error);attempt(audio,"external WAV");
        }
        for(const auto& c:cases)attempt(fixture(c.rate,c.channels,c.seconds,c.level,c.silence),"synthetic",
                                   c.rate==96000&&c.channels==1);
        std::cout<<"RESULT capture/reboot/rollback_pass="<<passed<<" failures="<<failed
            <<"; Windows/DAW validation is separate\n";
        return failed?1:0;
    } catch(const std::exception& ex){std::cerr<<"FAIL: "<<ex.what()<<std::endl;return 1;}
}
