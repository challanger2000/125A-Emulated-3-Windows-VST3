// Real packaged VST3 lifecycle/state probe. Firmware-dependent cases are opt-in
// through EIII_ROM_DIR/EIII_HD_IMAGE. No firmware is generated, downloaded or bundled.
#ifdef EIII_OFFLINE_IN_PROCESS
#include "eiii_offline_processor.inc"
#else
#define NOMINMAX
#include <windows.h>
#endif
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/eventlist.h"
#include "eiii_editor_win.h"
#include "project_state.h"
#include "core_section_container.h"
#include "core_checkpoint_bundle.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>
using namespace Steinberg;
using namespace Steinberg::Vst;
void require(bool condition,const char* why) {if(!condition)throw std::runtime_error(why);}
struct Stream final:IBStream {
 std::vector<uint8_t> bytes;int64 pos=0;std::atomic<uint32> refs{1};
 tresult PLUGIN_API queryInterface(const TUID iid,void** out) override {
  if(FUnknownPrivate::iidEqual(iid,IBStream::iid)||FUnknownPrivate::iidEqual(iid,FUnknown::iid)){*out=static_cast<IBStream*>(this);addRef();return kResultOk;}*out=nullptr;return kNoInterface;
 }
 uint32 PLUGIN_API addRef() override{return ++refs;}uint32 PLUGIN_API release() override{return --refs;}
 tresult PLUGIN_API read(void* p,int32 n,int32* done) override {
  if(n<0)return kInvalidArgument;const auto count=std::min<size_t>(size_t(n),bytes.size()-size_t(pos));
  if(count)std::memcpy(p,bytes.data()+pos,count);pos+=int64(count);if(done)*done=int32(count);return count? kResultOk:kResultFalse;
 }
 tresult PLUGIN_API write(void* p,int32 n,int32* done) override {
  if(n<0)return kInvalidArgument;if(size_t(pos)+size_t(n)>bytes.size())bytes.resize(size_t(pos)+size_t(n));
  if(n)std::memcpy(bytes.data()+pos,p,size_t(n));pos+=n;if(done)*done=n;return kResultOk;
 }
 tresult PLUGIN_API seek(int64 p,int32 mode,int64* result) override {
  int64 next=mode==0?p:mode==1?pos+p:int64(bytes.size())+p;
  if(next<0||next>int64(bytes.size()))return kInvalidArgument;pos=next;if(result)*result=pos;return kResultOk;
 }
 tresult PLUGIN_API tell(int64* p) override{if(!p)return kInvalidArgument;*p=pos;return kResultOk;}
};
void unchanged_import_bank(Stream const& before,Stream const& during) {
 using namespace eiii::vst3;
 DecodedState a,b;
 require(decode_state(before.bytes.data(),before.bytes.size(),a) &&
         decode_state(during.bytes.data(),during.bytes.size(),b),"decode in-flight host snapshots");
 require(a.value.rom_dir==b.value.rom_dir && a.value.hd_path==b.value.hd_path &&
         a.value.cd_path==b.value.cd_path && a.value.extra_kind==b.value.extra_kind &&
         a.value.extra_path==b.value.extra_path && a.value.hd_changes==b.value.hd_changes &&
         a.value.midi_channel==b.value.midi_channel && a.value.cem_model==b.value.cem_model &&
         a.value.autoload_bank==b.value.autoload_bank && a.value.autoload_preset==b.value.autoload_preset,
         "in-flight import changed committed project media/routing/recall");
 auto memory=[](auto const& state,auto& ram,auto& samples){
  std::vector<CoreSectionContainer::Section> sections;
  require(CoreSectionContainer::decode(state.machine_snapshot.data(),state.machine_snapshot.size(),sections),"in-flight machine container");
  CoreMidiSection::State midi;CoreExecutionSection::State execution;
  require(CoreCheckpointBundle::decode(sections[0].bytes.data(),sections[0].bytes.size(),ram,samples,midi,execution),"in-flight RAM checkpoint");
 };
 std::vector<uint16_t> ar,as,br,bs;memory(a.value,ar,as);memory(b.value,br,bs);
 require(as==bs,"in-flight import changed previous sample RAM/PCM");
 for(uint32_t address:{0x1a1b8u,0x1a344u}){
  auto index=(address-0x8000)/2;
  require(ar.at(index)==br.at(index)&&ar.at(index+1)==br.at(index+1),"in-flight import changed previous instrument/sample bounds");
 }
}
struct Receiver final:IConnectionPoint {
 EiiiUiState state{};std::atomic<uint32> refs{1};
 uint64_t last_dirty_revision=0;
 unsigned dirty_notifications=0;
 tresult PLUGIN_API queryInterface(const TUID iid,void** out) override {
  if(FUnknownPrivate::iidEqual(iid,IConnectionPoint::iid)||FUnknownPrivate::iidEqual(iid,FUnknown::iid)){*out=static_cast<IConnectionPoint*>(this);addRef();return kResultOk;}*out=nullptr;return kNoInterface;
 }
 uint32 PLUGIN_API addRef() override{return ++refs;}uint32 PLUGIN_API release() override{return --refs;}
 tresult PLUGIN_API connect(IConnectionPoint*) override{return kResultOk;}
 tresult PLUGIN_API disconnect(IConnectionPoint*) override{return kResultOk;}
 tresult PLUGIN_API notify(IMessage* m) override {
  if(m&&m->getMessageID()&&!std::strcmp(m->getMessageID(),"EIII.GUI.Dirty")) {
   int64 revision=0;
   require(m->getAttributes()&&
           m->getAttributes()->getInt("revision",revision)==kResultOk &&
           revision>0,"A10 dirty message must contain positive revision");
   require(uint64_t(revision)>last_dirty_revision,"A10 revision strictly advances");
   last_dirty_revision=uint64_t(revision);
   ++dirty_notifications;
  }
  if(m&&m->getMessageID()&&!std::strcmp(m->getMessageID(),"EIII.GUI.Status")) {
   const void* p=nullptr;uint32 n=0;
   if(m->getAttributes()->getBinary("payload",p,n)==kResultOk&&p&&n==sizeof(state))std::memcpy(&state,p,n);
  }return kResultOk;
 }
};
struct Instance {
 IComponent* component=nullptr;IAudioProcessor* processor=nullptr;IConnectionPoint* point=nullptr;
 HostApplication host;Receiver receiver;std::vector<float> captured;
 Instance(IPluginFactory* factory,const TUID cid,Stream* restore=nullptr) {
#ifdef EIII_OFFLINE_IN_PROCESS
  component=new Processor;
#else
  require(factory->createInstance(cid,IComponent::iid,reinterpret_cast<void**>(&component))==kResultOk,"create component");
#endif
  require(component->initialize(&host)==kResultOk,"initialize component");
  require(component->queryInterface(IAudioProcessor::iid,reinterpret_cast<void**>(&processor))==kResultOk,"query audio processor");
  require(component->queryInterface(IConnectionPoint::iid,reinterpret_cast<void**>(&point))==kResultOk,"query connection");
  require(point->connect(&receiver)==kResultOk,"connect status receiver");
  if(restore){restore->pos=0;require(component->setState(restore)==kResultOk,"restore before setupProcessing");}
  SpeakerArrangement input=SpeakerArr::kStereo,output=SpeakerArr::kStereo;
  SpeakerArrangement mono=SpeakerArr::kMono;
  // R05: the DSP maps exactly two output channels, and the ADC uses one
  // stereo auxiliary input. Do not accept layouts that would be misrouted.
  require(processor->setBusArrangements(&mono,1,&output,1)!=kResultOk,
          "R05 mono ADC input must be rejected");
  require(processor->setBusArrangements(&input,1,&mono,1)!=kResultOk,
          "R05 mono main output must be rejected");
  require(processor->setBusArrangements(nullptr,0,&output,1)!=kResultOk,
          "R05 missing ADC bus must be rejected");
  require(processor->setBusArrangements(&input,1,nullptr,0)!=kResultOk,
          "R05 missing output bus must be rejected");
  require(processor->setBusArrangements(&input,1,&output,1)==kResultOk,"stereo bus negotiation");
  ProcessSetup setup{};setup.processMode=kRealtime;setup.symbolicSampleSize=kSample32;setup.maxSamplesPerBlock=256;setup.sampleRate=48000;
  require(processor->setupProcessing(setup)==kResultOk,"setup processing");
  require(component->setActive(true)==kResultOk,"activate");require(processor->setProcessing(true)==kResultOk,"start processing");
 }
 ~Instance(){if(processor)processor->setProcessing(false);if(component)component->setActive(false);if(point){point->disconnect(&receiver);point->release();}if(processor)processor->release();if(component){component->terminate();component->release();}}
 void poll(){HostMessage m;m.setMessageID("EIII.GUI.Poll");require(point->notify(&m)==kResultOk,"status poll");}
 // Pitch is measured only after the actual async restore has committed and
 // at least one reported latency of complete realtime blocks has arrived.
 void ready_audio() {
  const auto limit=std::chrono::steady_clock::now()+std::chrono::seconds(30);
  do {
   poll();require(std::chrono::steady_clock::now()<limit,"full machine restore did not finish before pitch measurement");
   if(std::strstr(receiver.state.media_status,"project restored"))break;
   std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }while(true);
  float l[256]{},r[256]{};float* buffers[]={l,r};AudioBusBuffers output{};
  output.numChannels=2;output.channelBuffers32=buffers;
  ProcessData data{};data.processMode=kRealtime;data.symbolicSampleSize=kSample32;
  data.numSamples=256;data.numOutputs=1;data.outputs=&output;
  uint64_t healthy=0,blocks=0;const auto start=std::chrono::steady_clock::now();
  while(healthy<std::max<uint32_t>(256,receiver.state.latency_frames)) {
   const auto before=receiver.state;
   require(processor->process(data)==kResultOk,"prime realtime audio");poll();
   if(receiver.state.rendered_frames-before.rendered_frames==256 &&
      receiver.state.underrun_frames==before.underrun_frames)healthy+=256;
   else healthy=0;
   require(std::chrono::steady_clock::now()<limit,"realtime audio did not supply complete blocks");
   std::this_thread::sleep_until(start+std::chrono::nanoseconds(int64(++blocks*256*1000000000ull/48000)));
  }
 }
 void import(const std::string& path,unsigned root=60,unsigned low=48,unsigned high=72) {
  HostMessage m;m.setMessageID("EIII.GUI.WavImport");auto* a=m.getAttributes();a->setBinary("path",path.data(),uint32(path.size()));
  a->setInt("root",root);a->setInt("low",low);a->setInt("high",high);a->setInt("preset",0);
  require(point->notify(&m)==kResultOk,"submit WAV import");
 }
 double note(int pitch,double seconds) {
  poll();const auto before=receiver.state;captured.clear();
  EventList events;Event note{};note.type=Event::kNoteOnEvent;note.noteOn.channel=13;note.noteOn.pitch=int16(pitch);note.noteOn.velocity=1;events.addEvent(note);
  float l[256]{},r[256]{};float* buffers[]={l,r};AudioBusBuffers output{};output.numChannels=2;output.channelBuffers32=buffers;
  ProcessData data{};data.processMode=kRealtime;data.symbolicSampleSize=kSample32;data.numSamples=256;data.numOutputs=1;data.outputs=&output;data.inputEvents=&events;
  double power=0;size_t samples=0;const auto start=std::chrono::steady_clock::now();
  const int blocks=int(seconds*48000/256);
  for(int n=0;n<blocks;++n) {
   require(processor->process(data)==kResultOk,"audio process");events.clear();
   for(int k=0;k<256;++k){captured.push_back(l[k]);require(std::isfinite(l[k])&&std::isfinite(r[k]),"finite stereo output");power+=double(l[k])*l[k]+double(r[k])*r[k];samples+=2;}
   std::this_thread::sleep_until(start+std::chrono::nanoseconds(int64(uint64_t(n+1)*256*1000000000ull/48000)));
  }
  note.type=Event::kNoteOffEvent;note.noteOff.channel=13;note.noteOff.pitch=int16(pitch);note.noteOff.velocity=0;events.addEvent(note);
  require(processor->process(data)==kResultOk,"note release");
  poll();const double rms=samples?std::sqrt(power/double(samples)):0;
  std::cout<<"WAV HOST note="<<pitch<<" rms="<<rms<<" actual_frames="
           <<receiver.state.rendered_frames-before.rendered_frames<<" underrun_frames="
           <<receiver.state.underrun_frames-before.underrun_frames<<std::endl;
  require(receiver.state.rendered_frames-before.rendered_frames==uint64_t(blocks+1)*256 &&
          receiver.state.underrun_frames==before.underrun_frames,"pitch fixture contains missing realtime frames");
  if(const char* dump=std::getenv("EIII_HOST_AUDIO_DUMP")) {
   std::ofstream file(std::string(dump)+"-"+std::to_string(pitch)+".f32",std::ios::binary);
   file.write(reinterpret_cast<const char*>(captured.data()),std::streamsize(captured.size()*sizeof(float)));
  }
  return rms;
 }
};
double strongest(const std::vector<float>& samples,double expected) {
 const size_t start=std::min<size_t>(samples.size(),7200),end=std::min<size_t>(samples.size(),36000);
 require(end>start,"frequency measurement has audio");double best=0,bestPower=0;
 for(double hz=expected-5;hz<=expected+5;hz+=.5){
  const double c=2*std::cos(2*3.141592653589793*hz/48000);double s1=0,s2=0;
  for(size_t i=start;i<end;++i){const double next=samples[i]+c*s1-s2;s2=s1;s1=next;}
  const double power=s1*s1+s2*s2-c*s1*s2;if(power>bestPower){bestPower=power;best=hz;}
 }
 return best;
}
unsigned option(const char* name,unsigned fallback) {const char* p=std::getenv(name);return p?unsigned(std::strtoul(p,nullptr,10)):fallback;}
void put(std::ofstream& f,uint32_t x,unsigned n=4){for(unsigned k=0;k<n;++k)f.put(char(x>>(8*k)));}
void wave(const std::filesystem::path& path) {
 const unsigned rate=option("EIII_WAV_TEST_RATE",48000),channels=option("EIII_WAV_TEST_CHANNELS",2),bits=option("EIII_WAV_TEST_BITS",16);
 const bool floating=bits==32;
 require((channels==1||channels==2)&&(bits==16||bits==24||bits==32)&&rate>=8000&&rate<=192000,"fixture options");
 const unsigned frames=rate,align=channels*(bits/8),bytes=frames*align;
 std::ofstream f(path,std::ios::binary);
 f.write("RIFF",4);put(f,36+bytes);f.write("WAVEfmt ",8);put(f,16);put(f,floating?3:1,2);put(f,channels,2);put(f,rate);put(f,rate*align);put(f,align,2);put(f,bits,2);f.write("data",4);put(f,bytes);
 for(unsigned i=0;i<frames;++i)for(unsigned c=0;c<channels;++c){
  const double level=option("EIII_WAV_TEST_QUIET",0)?.02:.35;
  const bool silent=option("EIII_WAV_TEST_LEAD_SILENCE",0)&&i<rate/100;
  // Distinct nonperiodic channel fingerprints expose lost WAV prefixes and
  // incorrect L/R offsets that a plain periodic 1kHz tone can conceal.
  const uint32_t tag=(i+1u)*1664525u+1013904223u+uint32_t(c)*0x9e3779b9u;
  const double fingerprint=(double((tag>>16)&0xffffu)/32768.0-1.0)*.03;
  const float value=silent?0.f:float(level*((c?.5:1.)*std::sin(2*3.141592653589793*1000*i/rate)+fingerprint));
  if(floating){uint32_t v=0;std::memcpy(&v,&value,4);put(f,v);}
  else put(f,uint32_t(int32_t(value*(bits==16?32768.:8388608.))),bits/8);
 }
 require(bool(f),"write synthetic WAV");
}
#ifdef EIII_OFFLINE_IN_PROCESS
int main() {
#else
int wmain(int argc,wchar_t** argv) {
#endif
 try {
#ifdef EIII_OFFLINE_IN_PROCESS
  IPluginFactory* factory=nullptr;PClassInfo cls{};
#else
  require(argc==2,"usage: eiii_wav_host_test <packaged VST3 binary>");
  HMODULE module=LoadLibraryW(argv[1]);require(module!=nullptr,"load packaged module");
  // On Windows the VST3 module is initialized by DllMain during LoadLibrary.
  // InitDll and ModuleEntry are not required exports on this platform.
  // The Steinberg validator already loads this same binary successfully.

  auto factory=reinterpret_cast<IPluginFactory*(*)()>(GetProcAddress(module,"GetPluginFactory"))();require(factory!=nullptr,"factory");
  PClassInfo cls{};require(factory->getClassInfo(0,&cls)==kResultOk,"processor class");
#endif
  const bool firmware=std::getenv("EIII_ROM_DIR")&&std::getenv("EIII_HD_IMAGE");
  const auto path=std::filesystem::temp_directory_path()/("125a-wav-host-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".wav");wave(path);
  Stream state;
  {
   Instance first(factory,cls.cid),second(factory,cls.cid);
   first.poll();second.poll();require(!first.receiver.state.wav_busy&&!second.receiver.state.wav_busy,"independent initial instances");
   require(first.receiver.state.analog_model==2 && second.receiver.state.analog_model==2,
           "CEM3387 V2 must be selected for every newly created VST3 instrument");
   {
    eiii::vst3::ProjectState existing;
    existing.cem_model="v1"; // legitimate user selection in an old song
    Stream savedLegacy;
    require(eiii::vst3::encode_state(existing,savedLegacy.bytes),"encode explicit CEM V1 song");
    Instance restoredOldSong(factory,cls.cid,&savedLegacy);
    restoredOldSong.poll();
    require(restoredOldSong.receiver.state.analog_model==1,
            "restoring existing project must NOT silently switch explicitly saved CEM V1 to V2");
   }
   // R03: time production GUI status polling with a huge SAVED project
   // change set. This ROM-free measurement is not a firmware/underrun test.
   {
    using clock=std::chrono::steady_clock;
    eiii::vst3::ProjectState large{};
    large.hd_path="Z:/125A-R03-nonexistent-disk.img";
    large.hd_changes[0].assign(48u*1024u*1024u,'x');
    Stream projectState;
    require(eiii::vst3::encode_state(large,projectState.bytes),"R03 build 48 MiB overlay fixture");
    large.hd_changes[0].clear(); // drop temporary
    Instance overlay(factory,cls.cid,&projectState);
    overlay.poll();
    constexpr size_t count=750;
    std::vector<double> measurements;
    measurements.reserve(count);
    for(size_t i=0;i<count;++i) {
     const auto started=clock::now();
     overlay.poll();
     measurements.push_back(std::chrono::duration<double,std::milli>(clock::now()-started).count());
     if((i%40)==0) {first.poll();second.poll();}
    }
    std::sort(measurements.begin(),measurements.end());
    std::cout<<"R03 GUI poll 48 MiB synthetic saved overlay, 3 VST3 instances, 750 polls"
             <<" median_ms="<<measurements[count/2]
             <<" p95_ms="<<measurements[count*95/100]
             <<" p99_ms="<<measurements[count*99/100]
             <<" max_ms="<<measurements.back()<<"\\n";
    // Source guard independently rejects full-overlay copies in routine
    // GUI poll paths; no fabricated realtime tail-latency PASS here.
    require(overlay.receiver.state.project_dirty_revision==0,
            "R03 status-only GUI polling changed dirty revision");
   }

   // A10 integration: actual packaged processor -> IConnectionPoint message.
   // Use second instance so the WAV test can keep its own baseline.
   {
    auto& probe=second;
    const auto initial=probe.receiver.state.project_dirty_revision;
    const auto count=probe.receiver.dirty_notifications;
    probe.poll();probe.poll();
    require(probe.receiver.state.project_dirty_revision==initial &&
            probe.receiver.dirty_notifications==count,
            "A10 status-only polls must not mark song dirty");
    HostMessage autoRecall;autoRecall.setMessageID("EIII.GUI.AutoRecall");
    autoRecall.getAttributes()->setInt("bank",4);
    autoRecall.getAttributes()->setInt("preset",-1);
    require(probe.point->notify(&autoRecall)==kResultOk,"A10 accept D1 autoload");
    probe.poll();
    require(probe.receiver.state.project_dirty_revision>initial &&
            probe.receiver.dirty_notifications==count+1 &&
            probe.receiver.last_dirty_revision==probe.receiver.state.project_dirty_revision,
            "A10 accepted auto-recall sends immediate dirty revision");
    const auto after=probe.receiver.dirty_notifications;
    require(probe.point->notify(&autoRecall)==kResultOk,"A10 duplicate recall accepted");
    probe.poll();
    require(probe.receiver.dirty_notifications==after,
            "A10 duplicate identical recall must not signal dirty");
    autoRecall.getAttributes()->setInt("bank",-5);
    require(probe.point->notify(&autoRecall)!=kResultOk,"A10 reject invalid recall");
    probe.poll();
    require(probe.receiver.dirty_notifications==after,
            "A10 rejected recall must not signal dirty");
    HostMessage channel;channel.setMessageID("EIII.GUI.MidiRouting");
    channel.getAttributes()->setInt("target",11);
    require(probe.point->notify(&channel)==kResultOk,"A10 accept host MIDI route");
    probe.poll();
    require(probe.receiver.dirty_notifications==after+1,
            "A10 changed MIDI route must signal dirty");
    require(probe.point->notify(&channel)==kResultOk,"A10 duplicate route accepted");
    probe.poll();
    require(probe.receiver.dirty_notifications==after+1,
            "A10 duplicate route must not signal dirty");
    channel.getAttributes()->setInt("target",99);
    require(probe.point->notify(&channel)!=kResultOk,"A10 invalid route rejected");
    probe.poll();
    require(probe.receiver.dirty_notifications==after+1,
            "A10 invalid route must not signal dirty");
    std::cout << "PASS A10 native VST3: accepted edits dirty; duplicate/rejected/polls clean\\n";
   }
   if(firmware){
    const auto boot_timeout=std::chrono::steady_clock::now()+std::chrono::minutes(5);
    while(first.receiver.state.lcd[0][0]!='P') {first.poll();std::this_thread::sleep_for(std::chrono::milliseconds(10));require(std::chrono::steady_clock::now()<boot_timeout,"first firmware boot timeout");}
   }
   Stream before;
   const auto save_timeout=std::chrono::steady_clock::now()+std::chrono::seconds(30);
   while(first.component->getState(&before)!=kResultOk) {
    require(firmware&&std::chrono::steady_clock::now()<save_timeout,"save prior state after firmware initialization");
    before.bytes.clear();before.pos=0;first.poll();std::this_thread::sleep_for(std::chrono::milliseconds(1));
   }
   first.import(path.string());
   Stream during;require(first.component->getState(&during)==kResultOk,"save during import");
   if(firmware){
    // Full B02 CPU/timer checkpoints at two different instants legitimately
    // differ. Verify the unchanged bank/media and a stable in-flight snapshot.
    unchanged_import_bank(before,during);
    first.poll();require(first.receiver.state.wav_busy,"in-flight test did not overlap firmware import");
    before.pos=0;require(first.component->setState(&before)==kResultFalse,"project restore interrupted WAV import");
    Stream repeated;require(first.component->getState(&repeated)==kResultOk,"repeat in-flight state capture");
    require(repeated.bytes==during.bytes,"in-flight pre-import machine checkpoint changed");
   }
   if(!firmware){HostMessage cancel;cancel.setMessageID("EIII.GUI.WavCancel");first.point->notify(&cancel);}
   const auto timeout=std::chrono::steady_clock::now()+std::chrono::minutes(firmware?10:1);
   // Emit actionable firmware diagnostics during long imports. The batch
   // launcher captures stdout in WAV-Test-Ergebnis.txt even on a timeout.
   auto next_report=std::chrono::steady_clock::now()+std::chrono::seconds(10);
   auto report=[&](const char* label) {
    first.poll();
    const auto& st=first.receiver.state;
    std::cout<<label<<" firmware_seconds="<<st.seconds
             <<" running="<<st.running<<" wav_busy="<<st.wav_busy
             <<" wav_serial="<<st.wav_serial
             <<" media_status="<<st.media_status<<" status="<<st.status<<"\\n";
    for(const auto& line:st.lcd)std::cout<<"LCD: "<<line<<"\\n";
    std::cout<<std::flush;
   };
   report("WAV START");
   do{
    first.poll();std::this_thread::sleep_for(std::chrono::milliseconds(10));
    const auto now=std::chrono::steady_clock::now();
    if(now>=next_report){report("WAV PROGRESS");next_report=now+std::chrono::seconds(10);}
    if(now>=timeout){report("WAV TIMEOUT");throw std::runtime_error("import completion timeout; see firmware/LCD progress above");}
   }while(!first.receiver.state.wav_serial);
   report("WAV DONE");
   if(firmware){std::cout<<first.receiver.state.media_status<<"\n";require(std::strstr(first.receiver.state.media_status,"verified")!=nullptr,"firmware import chain NOT verified");}
   else require(std::strstr(first.receiver.state.media_status,"failed")!=nullptr,"missing firmware must not report import success");
   second.poll();require(second.receiver.state.wav_serial==0,"second instance unaffected");
   require(first.component->getState(&state)==kResultOk&&!state.bytes.empty(),"save actual component state");
  }
  // Studio One regression: cancel an import, remove the VST3 instance,
  // then instantiate and activate the same VST3 binary again.
  // Must work without restarting the host process or reloading the DLL.
  for(int repeat=0;repeat<3;++repeat) {
   Instance restarted(factory,cls.cid,&state);
   restarted.poll();
   // Write into a fresh stream. Reusing the prior stream after setState()
   // would append a second serialized state and make the next restore invalid.
   Stream nextState;
   const auto ready=std::chrono::steady_clock::now()+std::chrono::seconds(30);
   while(restarted.component->getState(&nextState)!=kResultOk) {
    require(firmware&&std::chrono::steady_clock::now()<ready,"repeated fresh-instance B02 restore did not complete");
    nextState.bytes.clear();nextState.pos=0;restarted.poll();std::this_thread::sleep_for(std::chrono::milliseconds(1));
   }
   require(!nextState.bytes.empty(),"repeated fresh-instance state capture after cancelled WAV import");
   state.bytes=std::move(nextState.bytes);
   state.pos=0;
  }
  std::cout<<"PASS remove and reinsert VST3 three times after WAV cancel\\n";
  std::filesystem::remove(path); // recall must not depend on the source WAV
  {
   Instance fresh(factory,cls.cid,&state);fresh.poll();
   if(firmware){
    fresh.ready_audio();
    const auto until=std::chrono::steady_clock::now()+std::chrono::minutes(5);double rms=0;
    while((rms=fresh.note(60,1))<=.001){fresh.poll();require(std::chrono::steady_clock::now()<until,"fresh imported-bank audio recall timeout");}
    const double rootHz=strongest(fresh.captured,1000);
    std::cout<<"WAV HOST root frequency="<<rootHz<<" Hz"<<std::endl;
    require(std::abs(rootHz-1000)<3,"root MIDI 60 preserves 1kHz pitch");
    fresh.note(73,3);require(fresh.note(73,1)<.001,"note above high key is silent");
    require(fresh.note(48,1)>.001,"low key plays");require(std::abs(strongest(fresh.captured,500)-500)<3,"low key transposes down one octave");
    require(fresh.note(72,1)>.001,"high key plays");require(std::abs(strongest(fresh.captured,2000)-2000)<3,"high key transposes up one octave");
    std::cout<<"PASS firmware capture/placement/bank save, deleted-source fresh recall, root/range and octave transposition\n";
   }
  }
  std::filesystem::remove(path);
#ifndef EIII_OFFLINE_IN_PROCESS
  factory->release();const auto exit=reinterpret_cast<bool(*)()>(GetProcAddress(module,"ExitDll"));if(exit)exit();FreeLibrary(module);
#endif
  std::cout<<"PASS packaged lifecycle, two instances, stop/cancel and fresh component state restore\n";
  if(!firmware)std::cout<<"NOT RUN: firmware recording, placement, bank save and audible project recall (set EIII_ROM_DIR/EIII_HD_IMAGE)\n";
  return 0;
 }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}
}
