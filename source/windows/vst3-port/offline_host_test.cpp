// Actual VST3 processor test: Windows loads the packaged module. The optional
// native runner compiles the production Processor with the real Steinberg SDK.
#ifdef EIII_OFFLINE_IN_PROCESS
#include "eiii_offline_processor.inc"
#elif defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#elif !defined(EIII_OFFLINE_HOST_FACTORY_COMPILE_ONLY)
#error Use the native runner or a Windows VST3 module
#endif
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
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
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

namespace offline_test {
using namespace Steinberg;
using namespace Steinberg::Vst;
void check(bool ok,const char* why) {if(!ok) throw std::runtime_error(why);}
struct Stream final : IBStream {
 std::vector<uint8_t> bytes;int64 pos=0;std::atomic<uint32> refs{1};
 tresult PLUGIN_API queryInterface(const TUID iid,void** out) override {
  if(FUnknownPrivate::iidEqual(iid,IBStream::iid)||FUnknownPrivate::iidEqual(iid,FUnknown::iid)) {
   *out=static_cast<IBStream*>(this);addRef();return kResultOk;
  }*out=nullptr;return kNoInterface;
 }
 uint32 PLUGIN_API addRef() override {return ++refs;}
 uint32 PLUGIN_API release() override {return --refs;}
 tresult PLUGIN_API read(void* p,int32 n,int32* done) override {
  if(n<0)return kInvalidArgument;const auto count=std::min<size_t>(size_t(n),bytes.size()-size_t(pos));
  if(count)std::memcpy(p,bytes.data()+pos,count);pos+=int64(count);
  if(done)*done=int32(count);return count?kResultOk:kResultFalse;
 }
 tresult PLUGIN_API write(void* p,int32 n,int32* done) override {
  if(n<0)return kInvalidArgument;if(size_t(pos)+size_t(n)>bytes.size())bytes.resize(size_t(pos)+size_t(n));
  if(n)std::memcpy(bytes.data()+pos,p,size_t(n));pos+=n;if(done)*done=n;return kResultOk;
 }
 tresult PLUGIN_API seek(int64 p,int32 mode,int64* done) override {
  const int64 next=mode==0?p:mode==1?pos+p:int64(bytes.size())+p;
  if(next<0||next>int64(bytes.size()))return kInvalidArgument;pos=next;if(done)*done=pos;return kResultOk;
 }
 tresult PLUGIN_API tell(int64* p) override {if(!p)return kInvalidArgument;*p=pos;return kResultOk;}
};
struct Receiver final : IConnectionPoint {
 EiiiUiState state{};std::atomic<uint32> refs{1};
 tresult PLUGIN_API queryInterface(const TUID iid,void** out) override {
  if(FUnknownPrivate::iidEqual(iid,IConnectionPoint::iid)||FUnknownPrivate::iidEqual(iid,FUnknown::iid)) {
   *out=static_cast<IConnectionPoint*>(this);addRef();return kResultOk;
  }*out=nullptr;return kNoInterface;
 }
 uint32 PLUGIN_API addRef() override {return ++refs;}
 uint32 PLUGIN_API release() override {return --refs;}
 tresult PLUGIN_API connect(IConnectionPoint*) override {return kResultOk;}
 tresult PLUGIN_API disconnect(IConnectionPoint*) override {return kResultOk;}
 tresult PLUGIN_API notify(IMessage* m) override {
  if(m&&m->getMessageID()&&!std::strcmp(m->getMessageID(),"EIII.GUI.Status")) {
   const void* p=nullptr;uint32 n=0;
   if(m->getAttributes()->getBinary("payload",p,n)==kResultOk&&p&&n==sizeof(state))std::memcpy(&state,p,n);
  }return kResultOk;
 }
};
struct Note {uint64_t frame;int pitch;bool on;int channel=13;};
struct Render {
 std::vector<float> lr;double peak=0,rms=0,elapsed=0;uint64_t first=0;
 EiiiUiState before{},after{};
 uint64_t expected_midi_hash=0;
};
struct Instance {
 HostApplication host;Receiver receiver;
 IComponent* component=nullptr;IAudioProcessor* audio=nullptr;IConnectionPoint* point=nullptr;
 double rate;int maxBlock=1024;bool active=false,processing=false;
 Instance(IPluginFactory* factory,const TUID cid,double sr,Stream* state=nullptr,bool autostart=true):rate(sr) {
#ifdef EIII_OFFLINE_IN_PROCESS
  component=new Processor;
#else
  check(factory->createInstance(cid,IComponent::iid,reinterpret_cast<void**>(&component))==kResultOk,"create processor");
#endif
  check(component->initialize(&host)==kResultOk,"initialize");
  check(component->queryInterface(IAudioProcessor::iid,reinterpret_cast<void**>(&audio))==kResultOk,"audio interface");
  check(component->queryInterface(IConnectionPoint::iid,reinterpret_cast<void**>(&point))==kResultOk,"connection interface");
  check(point->connect(&receiver)==kResultOk,"connect receiver");
  if(state){state->pos=0;check(component->setState(state)==kResultOk,"restore before activation");}
  SpeakerArrangement in=SpeakerArr::kStereo,out=SpeakerArr::kStereo;
  check(audio->setBusArrangements(&in,1,&out,1)==kResultOk,"stereo buses");
  for(int bus=0;bus<1;++bus){component->activateBus(kAudio,kOutput,bus,true);component->activateBus(kEvent,kInput,bus,true);}
  if(autostart)mode(kOffline,1024);
 }
 ~Instance() {
  if(processing)audio->setProcessing(false);
  if(active)component->setActive(false);
  if(point){point->disconnect(&receiver);point->release();}
  if(audio)audio->release();
  if(component){component->terminate();component->release();}
 }
 void mode(int mode,int maximum,bool cycle=true) {
  check(audio->setProcessing(false)==kResultOk,"stop processing");processing=false;
  if(active&&cycle){check(component->setActive(false)==kResultOk,"deactivate");active=false;}
  ProcessSetup setup{};setup.processMode=mode;setup.symbolicSampleSize=kSample32;
  setup.maxSamplesPerBlock=maximum;setup.sampleRate=rate;
  check(audio->setupProcessing(setup)==kResultOk,"setup mode");maxBlock=maximum;
  if(!active){check(component->setActive(true)==kResultOk,"activate");active=true;}
  check(audio->setProcessing(true)==kResultOk,"start processing");processing=true;
 }
 void poll() {HostMessage m;m.setMessageID("EIII.GUI.Poll");check(point->notify(&m)==kResultOk,"poll");}
 Render render(int mode,int block,double duration,const std::vector<Note>& notes={},bool paced=false) {
  check(block<=maxBlock,"block exceeds setup");poll();Render result;result.before=receiver.state;
  const uint64_t frames=uint64_t(std::llround(rate*duration));result.lr.reserve(size_t(frames)*2);
  result.expected_midi_hash=result.before.midi_trace_hash;
  auto byte=[&](uint8_t b){result.expected_midi_hash=(result.expected_midi_hash^b)*1099511628211ull;};
  for(const auto& note:notes) {
   const auto at=result.before.rendered_frames+result.before.latency_frames+note.frame;
   for(unsigned k=0;k<8;++k)byte(uint8_t(at>>(8*k)));
   const auto ch=result.before.forwarded_midi_channel?result.before.forwarded_midi_channel-1:uint32_t(note.channel);
   byte(3);byte(uint8_t((note.on?0x90:0x80)|(ch&15)));byte(uint8_t(note.pitch));byte(note.on?127:0);
  }
  EventList events;std::vector<float> l(block),r(block);float* buffers[]={l.data(),r.data()};
  AudioBusBuffers output{};output.numChannels=2;output.channelBuffers32=buffers;
  ProcessData data{};data.processMode=mode;data.symbolicSampleSize=kSample32;
  data.numOutputs=1;data.outputs=&output;data.inputEvents=&events;
  ProcessContext context{};context.sampleRate=rate;context.tempo=120;
  context.timeSigNumerator=4;context.timeSigDenominator=4;
  context.state=ProcessContext::kPlaying|ProcessContext::kProjectTimeMusicValid|ProcessContext::kTempoValid|ProcessContext::kTimeSigValid;
  data.processContext=&context;size_t next=0;
  const auto start=std::chrono::steady_clock::now();
  for(uint64_t f=0;f<frames;) {
   const int n=int(std::min<uint64_t>(block,frames-f));data.numSamples=n;events.clear();
   context.projectTimeSamples=int64(f);context.projectTimeMusic=double(f)/rate*2;
   while(next<notes.size()&&notes[next].frame<f+uint64_t(n)) {
    const auto& note=notes[next++];check(note.frame>=f,"unsorted MIDI");Event e{};
    e.busIndex=0;e.sampleOffset=int32(note.frame-f);e.ppqPosition=double(note.frame)/rate*2;
    if(note.on){e.type=Event::kNoteOnEvent;e.noteOn.channel=int16(note.channel);e.noteOn.pitch=int16(note.pitch);e.noteOn.velocity=1;e.noteOn.noteId=-1;}
    else {e.type=Event::kNoteOffEvent;e.noteOff.channel=int16(note.channel);e.noteOff.pitch=int16(note.pitch);e.noteOff.noteId=-1;}
    events.addEvent(e);
   }
   check(audio->process(data)==kResultOk,"process");
   for(int i=0;i<n;++i){check(std::isfinite(l[i])&&std::isfinite(r[i]),"nonfinite audio");result.lr.push_back(l[i]);result.lr.push_back(r[i]);}
   f+=uint64_t(n);
   if(paced)std::this_thread::sleep_until(start+std::chrono::nanoseconds(int64(double(f)*1e9/rate)));
  }
  result.elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();poll();result.after=receiver.state;
  double power=0;result.first=frames;
  for(size_t i=0;i<result.lr.size();++i){const double v=result.lr[i];power+=v*v;result.peak=std::max(result.peak,std::abs(v));if(std::abs(v)>1e-4)result.first=std::min<uint64_t>(result.first,i/2);}
  result.rms=std::sqrt(power/std::max<size_t>(1,result.lr.size()));
  return result;
 }
 void warmup() {
  // Advance the original boot/recall menus BEFORE the measurement. Never hide
  // a restart during the measured mode switch with another warmup.
  render(kOffline,1024,25);poll();
  check(receiver.state.running&&receiver.state.lcd[0][0]=='P'&&receiver.state.lcd[0][3]==' ',"firmware/recall not ready");
 }
 void save(Stream& s) {s.bytes.clear();s.pos=0;check(component->getState(&s)==kResultOk,"getState");}
#ifdef EIII_OFFLINE_IN_PROCESS
 uint64_t bankHash() {
  std::promise<uint64_t> reply;auto done=reply.get_future();
  check(static_cast<Processor*>(component)->engine_.inspect([&](eiii::runtime& rt){
   std::vector<uint16_t> ram,samples;
   check(rt.copy_volatile_memory(ram,samples),"bank RAM capture");
   const auto top=rt.debug_read32(0x1a344);
   check(top>=0x400000&&top<=0xbfd800&&!(top&1),"allocated bank extent");
   uint64_t hash=14695981039346656037ull;
   for(size_t i=0;i<(top-0x400000)/2;++i)hash=(hash^samples.at(i))*1099511628211ull;
   reply.set_value(hash);
  }),"queue bank diagnostic");
  check(done.wait_for(std::chrono::seconds(5))==std::future_status::ready,"bank diagnostic timeout");
  const auto hash=done.get();eiii::media_result result;
  // Drain completion through the ordinary non-audio owner before another save.
  const auto limit=std::chrono::steady_clock::now()+std::chrono::seconds(5);
  while(!static_cast<Processor*>(component)->engine_.poll_media_result(result)) {
   check(std::chrono::steady_clock::now()<limit,"bank diagnostic result timeout");std::this_thread::yield();
  }
  check(result.ok,"bank diagnostic failed");return hash;
 }
#endif
 void import(const char* path) {
  HostMessage m;m.setMessageID("EIII.GUI.WavImport");auto* a=m.getAttributes();
  a->setBinary("path",path,uint32(std::strlen(path)));a->setInt("root",60);a->setInt("low",36);a->setInt("high",84);a->setInt("preset",0);
  check(point->notify(&m)==kResultOk,"submit WAV");
  const auto deadline=std::chrono::steady_clock::now()+std::chrono::minutes(2);
  do {check(std::chrono::steady_clock::now()<deadline,"WAV import timeout");std::this_thread::sleep_for(std::chrono::milliseconds(5));poll();}
  while(receiver.state.wav_busy||!receiver.state.wav_serial);
  check(std::strstr(receiver.state.media_status,"verified")!=nullptr,"WAV import failed");
 }
};
// CPU/timer/audio state must advance while playing. Compare persistent project
// configuration and the complete sample RAM, rather than demanding that a full
// B02 machine checkpoint be byte-identical before and after seven seconds.
bool same_bank_project(Stream const& a,Stream const& b) {
 using namespace eiii::vst3;
 DecodedState x,y;
 if(!decode_state(a.bytes.data(),a.bytes.size(),x)||!decode_state(b.bytes.data(),b.bytes.size(),y))return false;
 auto memory=[](ProjectState const& state,std::vector<uint16_t>& sound){
  if(state.machine_snapshot.empty())return true; // legacy media-only fixture
  std::vector<CoreSectionContainer::Section> sections;
  if(!CoreSectionContainer::decode(state.machine_snapshot.data(),state.machine_snapshot.size(),sections))return false;
  for(auto const& section:sections)if(section.id==1){
   std::vector<uint16_t> ram;CoreMidiSection::State midi;CoreExecutionSection::State cpu;
   return CoreCheckpointBundle::decode(section.bytes.data(),section.bytes.size(),ram,sound,midi,cpu);
  }
  return false;
 };
 std::vector<uint16_t> xs,ys;
 if(!memory(x.value,xs)||!memory(y.value,ys))return false;
 // A legacy state has no stored volatile bank; its established media-only
 // assertions still apply. Every newly captured B02 pair compares all words.
 if(!xs.empty()&&!ys.empty()&&xs!=ys)return false;
 x.value.machine_snapshot.clear();y.value.machine_snapshot.clear();
 std::vector<uint8_t> xb,yb;
 return encode_state(x.value,xb)&&encode_state(y.value,yb)&&xb==yb;
}
void validate(const Render& result,double rate,size_t events,const char* label) {
 const auto& a=result.after;const auto& b=result.before;
 std::cout<<"RENDER "<<label<<" rate="<<rate<<" peak="<<result.peak<<" rms="<<result.rms
  <<" first="<<result.first<<" seconds="<<result.elapsed<<" frames="<<a.rendered_frames-b.rendered_frames
  <<" underrun="<<a.underrun_frames-b.underrun_frames<<" midi="<<a.midi_events-b.midi_events
  <<" late_max="<<a.midi_late_frames<<" starts="<<a.engine_starts<<std::endl;
 check(result.peak>.01&&result.rms>.001,"nearly silent render");
 check(a.underrun_frames==b.underrun_frames,"audio underrun");
 check(a.rendered_frames-b.rendered_frames==result.lr.size()/2,"missing engine frames");
 const auto modeFrames=a.process_mode==kOffline?a.offline_frames-b.offline_frames:a.realtime_frames-b.realtime_frames;
 check(modeFrames==result.lr.size()/2,"actual process-mode frame accounting differs");
 check(a.midi_dropped==b.midi_dropped&&a.midi_events-b.midi_events==events,"lost MIDI event");
 check(a.midi_trace_hash==result.expected_midi_hash,"MIDI bytes, order or absolute sample timestamp differ");
 check(a.midi_late_frames<=1,"MIDI handoff exceeded the original CPU/audio frame boundary");
 check(a.engine_starts==b.engine_starts,"emulator restarted during render");
 check(result.first>=uint64_t(2*rate)+a.latency_frames,"MIDI played before bar 2 plus reported latency");
}
double rms(const Render& r,double rate,double from,double to) {
 double p=0;size_t n=0;
 for(size_t i=size_t(from*rate)*2;i<std::min(r.lr.size(),size_t(to*rate)*2);++i){p+=double(r.lr[i])*r.lr[i];++n;}
 return std::sqrt(p/std::max<size_t>(1,n));
}
void compare(const Render& reference,const Render& candidate,double rate) {
 check(candidate.rms>reference.rms*.9&&candidate.rms<reference.rms*1.1,"offline/realtime audio energy differs");
 // Original UART and firmware dispatch remain asynchronous to host blocks.
 // Exact timestamp handoff is checked separately; do not require identical
 // analog-model noise or voice-allocation phase across separate performances.
 const size_t n=size_t(rate*.12);
 double best=-1;
 for(int shift=-64;shift<=64;++shift) {
  const int64_t start=int64_t(candidate.first)+shift;
  if(start<0)continue;
  double cross=0,x2=0,y2=0;
  for(size_t i=0;i<n*2;++i) {
   const auto a=size_t(reference.first)*2+i,b=size_t(start)*2+i;
   if(a>=reference.lr.size()||b>=candidate.lr.size())break;
   const double x=reference.lr[a],y=candidate.lr[b];cross+=x*y;x2+=x*x;y2+=y*y;
  }
  if(x2>0&&y2>0)best=std::max(best,cross/std::sqrt(x2*y2));
 }
 std::cout<<"AUDIO waveform_correlation="<<best<<std::endl;
 check(best>.98,"offline/realtime waveform differs");
 // Repeated realtime performances already vary by >10% in their global
 // multi-voice peak. Check each musical/release window instead. The control
 // measurements differ by <=2.4%; 5% is an output-envelope comparison, never
 // a tolerance for dropped frames, MIDI timestamps, or stored WAV PCM.
 for(double at:{2.,2.2,2.4,2.6,2.8,3.,3.2,3.5,4.}) {
  const double x=rms(reference,rate,at,at+.2),y=rms(candidate,rate,at,at+.2);
  if(x>.001)check(y>x*.95&&y<x*1.05,"note/release audio envelope differs");
  else check(y<.001,"unexpected sound in release window");
 }
}
std::vector<Note> sequence(double rate) {
 auto f=[=](double seconds,uint64_t offset){return uint64_t(std::llround(seconds*rate))+offset;};
 return {{f(2,7),60,true},{f(2.22,11),60,false},{f(2.24,3),60,true},{f(2.25,5),60,false},
         {f(2.27,1),64,true},{f(2.6,9),64,false},{f(2.65,13),60,true},{f(3,17),60,false}};
}
int run(IPluginFactory* factory,const TUID cid) {
 if(!std::getenv("EIII_ROM_DIR")||!std::getenv("EIII_HD_IMAGE")) {
  std::cout<<"SKIP: original EIII_ROM_DIR/EIII_HD_IMAGE required; no audio PASS claimed\n";return 77;
 }
 size_t cases=0;
 for(double rate:{44100.,48000.}) {
  Stream state;
  if(const char* file=std::getenv("EIII_OFFLINE_STATE")) {
   std::ifstream in(file,std::ios::binary);check(bool(in),"open private state");state.bytes.assign(std::istreambuf_iterator<char>(in),{});
  } else {
   const char* wav=std::getenv("EIII_REPRO_WAV");check(wav!=nullptr,"set EIII_REPRO_WAV or EIII_OFFLINE_STATE");
   // A full audio snapshot is bound to its sample rate. Build each initial
   // machine at the rate under test; never reinterpret 48-kHz clocks as 44.1.
   Instance fixture(factory,cid,rate);fixture.warmup();fixture.import(wav);fixture.save(state);
  }
  Instance instance(factory,cid,rate,&state);
  const auto notes=sequence(rate);
  // Cold project restore is also measured without hiding initialization with
  // a host warmup. The offline worker must perform its own original recall.
  const auto cold=instance.render(kOffline,257,7,notes);
  validate(cold,rate,notes.size(),"cold-offline-restore");++cases;
  Stream coldState;instance.save(coldState);check(same_bank_project(coldState,state),"cold export changed saved project/bank/sample RAM");
  instance.mode(kRealtime,1024);const auto reference=instance.render(kRealtime,256,7,notes,true);
  validate(reference,rate,notes.size(),"realtime");++cases;
  const auto repeat=instance.render(kRealtime,256,7,notes,true);
  validate(repeat,rate,notes.size(),"realtime-repeat-control");compare(reference,repeat,rate);++cases;
  Stream before;instance.save(before);const auto starts=instance.receiver.state.engine_starts;
#ifdef EIII_OFFLINE_IN_PROCESS
  const auto bankHash=instance.bankHash();
#endif
  for(int block:{64,257,1024,65536,257}) {
   instance.mode(kOffline,std::max(block,1024));
   instance.poll();check(instance.receiver.state.engine_starts==starts,"mode switch rebooted loaded bank");
   const auto result=instance.render(kOffline,block,7,notes);
   validate(result,rate,notes.size(),"offline");compare(reference,result,rate);
   check(rms(result,rate,6,7)<reference.rms*.001,"stuck release/tail");
   Stream after;instance.save(after);check(same_bank_project(before,after),"export changed project/bank/sample RAM");++cases;
#ifdef EIII_OFFLINE_IN_PROCESS
   check(instance.bankHash()==bankHash,"export changed loaded sample RAM");
#endif
  }
  instance.mode(kRealtime,1024);const auto again=instance.render(kRealtime,1024,7,notes,true);
  validate(again,rate,notes.size(),"realtime-after-offline");compare(reference,again,rate);++cases;
  // Compatibility with a host changing ProcessData without deactivation/setup:
  // setup remains realtime, but ONLY the actual kOffline callback may block.
  const auto callback=instance.render(kOffline,257,7,notes);
  validate(callback,rate,notes.size(),"callback-mode");compare(reference,callback,rate);++cases;
  instance.mode(kOffline,1024);
  const auto boundary=(uint64_t(2*rate)+1023)/1024*1024;
  const std::vector<Note> edges={{boundary+63,60,true},{boundary+10239,60,false},
   {boundary+10240,60,true},{boundary+20479,60,false},{boundary+20480,64,true},{boundary+24576,64,false}};
  const auto edge=instance.render(kOffline,64,7,edges);
  validate(edge,rate,edges.size(),"block-edge-midi");++cases;
  const std::vector<Note> held={{uint64_t(2*rate)+7,60,true}};
  auto released=held;released.push_back({uint64_t(2.10*rate)+11,60,false});
  const auto release=instance.render(kOffline,257,7,released);
  validate(release,rate,2,"note-off-control");
  const auto hold=instance.render(kOffline,257,7,held);
  validate(hold,rate,1,"held-negative-control");
  std::cout<<"NOTE_OFF release_rms="<<rms(release,rate,2.5,3)<<" held_rms="<<rms(hold,rate,2.5,3)<<std::endl;
  check(rms(hold,rate,2.5,3)>.001&&rms(release,rate,2.5,3)<rms(hold,rate,2.5,3)*.1,"NoteOff has no audible effect");cases+=2;
  // Explicitly release the negative control; no stuck note is left at close.
  instance.render(kOffline,257,3,{{0,60,false}});
  Stream after;instance.save(after);check(same_bank_project(before,after),"final project/bank/sample RAM changed");
#ifdef EIII_OFFLINE_IN_PROCESS
  check(instance.bankHash()==bankHash,"final loaded bank changed");
#endif
  // ProcessData kPrefetch must override an offline setup without blocking.
  // A single block larger than the entire ring cannot be supplied in realtime.
  instance.mode(kOffline,65536);
  const auto prefetch=instance.render(kPrefetch,65536,65536/rate);
  check(prefetch.after.prefetch_frames-prefetch.before.prefetch_frames==65536,"prefetch mode not observed");
  check(prefetch.after.rendered_frames-prefetch.before.rendered_frames<65536,"prefetch incorrectly waited like offline");
  ProcessData flush{};flush.processMode=kOffline;flush.symbolicSampleSize=kSample32;
  check(instance.audio->process(flush)==kResultOk,"zero-frame flush");
  std::cout<<"PASS nonblocking prefetch and zero-frame flush rate="<<rate<<std::endl;
 }
 std::cout<<"PASS offline host audio cases="<<cases<<"; Studio One/Windows QA remains separate\n";return 0;
}
} // namespace offline_test
#ifndef EIII_OFFLINE_HOST_SUPPORT_ONLY
#ifdef EIII_OFFLINE_IN_PROCESS
int main(int argc,char** argv) {
#else
int wmain(int argc,wchar_t** argv) {
#endif
 try {
#ifdef EIII_OFFLINE_IN_PROCESS
  const Steinberg::TUID cid{};return offline_test::run(nullptr,cid);
#else
  offline_test::check(argc==2,"usage: eiii_offline_host_test <VST3 binary>");
  HMODULE module=LoadLibraryW(argv[1]);offline_test::check(module!=nullptr,"load VST3 module");
  auto getFactory=reinterpret_cast<Steinberg::IPluginFactory*(*)()>(GetProcAddress(module,"GetPluginFactory"));
  offline_test::check(getFactory!=nullptr,"GetPluginFactory export");auto* factory=getFactory();offline_test::check(factory!=nullptr,"factory");
  Steinberg::PClassInfo cls{};offline_test::check(factory->getClassInfo(0,&cls)==Steinberg::kResultOk,"processor class");
  const int result=offline_test::run(factory,cls.cid);factory->release();FreeLibrary(module);return result;
#endif
 }catch(const std::exception& e){std::cerr<<"FAIL offline host: "<<e.what()<<std::endl;return 1;}
}
#endif
