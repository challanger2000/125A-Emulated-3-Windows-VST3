// Real VST3 interfaces + original firmware. Never write emulated RAM.
#define EIII_OFFLINE_HOST_SUPPORT_ONLY
#include "offline_host_test.cpp"
#include "project_state.h"
#include "../../upstream/standalone/first_start_midi.h"
#include "../../upstream/panel/eiii_panel_model.h"
using namespace offline_test;
namespace {
void wait_project(Instance& i,bool success){
 const auto limit=std::chrono::steady_clock::now()+std::chrono::seconds(30);
 for(;;){
  i.poll();const std::string status=i.receiver.state.media_status;
  if(status.find(success?"project restored":"restore failed")!=std::string::npos)return;
  check(std::chrono::steady_clock::now()<limit,"async project transaction did not finish");
  // Host-owner polling only; no production audio wait is introduced.
  std::this_thread::sleep_for(std::chrono::milliseconds(1));
 }
}
void pump(Instance& i,double seconds=.35){i.render(kOffline,1024,seconds);}
void button(Instance& i,const char* id,bool down){const auto n=eiii::panel_find_control(id);check(n>=0,"panel control");HostMessage m;m.setMessageID("EIII.GUI.Control");m.getAttributes()->setInt("index",n);m.getAttributes()->setInt("down",down);check(i.point->notify(&m)==kResultOk,"panel event");}
void press(Instance& i,const char* id){button(i,id,true);pump(i,.08);if(std::strcmp(id,"power"))button(i,id,false);pump(i,.30);}
void data(Instance& i,uint16_t value){const auto n=eiii::panel_find_control("data_slider");check(n>=0,"DATA control");HostMessage m;m.setMessageID("EIII.GUI.Slider");m.getAttributes()->setInt("index",n);m.getAttributes()->setInt("adc",value);check(i.point->notify(&m)==kResultOk,"DATA pot");pump(i,.35);}
void routing(Instance& i,unsigned ch){HostMessage m;m.setMessageID("EIII.GUI.MidiRouting");m.getAttributes()->setInt("target",ch);check(i.point->notify(&m)==kResultOk,"MIDI route");}
std::string screen(Instance& i){i.poll();return eiii::firmware_menu_lcd(i.receiver.state.hardware.lcd);}
void globals(Instance& i){press(i,"master");press(i,"keypad_8");press(i,"keypad_7");check(screen(i).find("MIDI GLOBALS:")!=std::string::npos,"original Globals menu");}
void leave(Instance& i){press(i,"enter");press(i,"master");}
void expect(Instance& i,unsigned channel,const char* mode){const auto s=screen(i);std::cout<<s<<std::flush;check(std::regex_search(s,std::regex("Basic Channel:\\s*"+std::to_string(channel)+"(\\s|$)")),"wrong firmware Basic Channel");check(std::regex_search(s,std::regex(std::string("MIDI Mode:\\s*")+mode)),"wrong firmware MIDI Mode");}
void select(Instance& i,const std::string& pattern){for(unsigned n=0;n<=32;++n){if(std::regex_search(screen(i),std::regex(pattern)))return;data(i,uint16_t(0x7100*n/32));}throw std::runtime_error("original DATA/LCD selection failed");}
void custom(Instance& i,unsigned channel,const char* mode){globals(i);press(i,"cursor_down");select(i,"Basic Channel:\\s*"+std::to_string(channel)+"(\\s|$)");press(i,"cursor_down");select(i,std::string("MIDI Mode:\\s*")+mode);expect(i,channel,mode);leave(i);}
void note(Instance& i,unsigned channel,bool audible){const auto r=i.render(kOffline,257,2.5,{{7,60,true,int(channel-1)},{uint64_t(i.rate*.15)+7,60,false,int(channel-1)}});std::cout<<"MIDI channel="<<channel<<" peak="<<r.peak<<" rms="<<r.rms<<std::endl;check(audible?r.peak>.01:r.peak<.0001,"MIDI receive/audio mismatch");check(r.after.midi_events-r.before.midi_events==2&&r.after.midi_dropped==r.before.midi_dropped&&r.after.midi_trace_hash==r.expected_midi_hash,"MIDI packet/timestamp mismatch");check(r.after.underrun_frames==r.before.underrun_frames,"audio underrun");}
int run_midi(IPluginFactory* factory,const TUID cid){
 if(!std::getenv("EIII_ROM_DIR")||!std::getenv("EIII_HD_IMAGE")||!std::getenv("EIII_REPRO_WAV")){std::cout<<"SKIP original ROM/OS/WAV required\n";return 77;}
 Stream saved;
 Stream saved_immediately_after_edit;
 {
  Instance fresh(factory,cid,48000);fresh.warmup();globals(fresh);expect(fresh,1,"omni");leave(fresh);
  check(fresh.receiver.state.forwarded_midi_channel==0,"fresh host routing changed");
  fresh.import(std::getenv("EIII_REPRO_WAV"));pump(fresh,1);globals(fresh);expect(fresh,1,"omni");leave(fresh);
  for(unsigned ch=1;ch<=16;++ch)note(fresh,ch,true);
  check(fresh.receiver.state.forwarded_midi_channel==0,"WAV import overwrote routing");
  custom(fresh,11,"poly");note(fresh,11,true);note(fresh,12,false);
  // Match Studio One: save the host state directly after firmware ENTER / MASTER,
  // without power-cycling the emulated hardware or allowing a second boot.
  fresh.save(saved_immediately_after_edit);
  check(!saved_immediately_after_edit.bytes.empty(),"empty immediate host state");
  // A later hardware power cycle of this instance must preserve the user's edit.
  press(fresh,"power");pump(fresh,.2);press(fresh,"power");pump(fresh,25);
  globals(fresh);expect(fresh,11,"poly");leave(fresh);note(fresh,11,true);
  fresh.save(saved);
 }
 {
  // This is the user's Studio One workflow. Earlier tests covered only
  // a power-cycled instance, potentially masking delayed/unflushed state.
  Instance immediate(factory,cid,48000,&saved_immediately_after_edit);
  immediate.warmup();globals(immediate);expect(immediate,11,"poly");leave(immediate);
 }
 {
  // Adversarial VST3 host ordering: Studio One may activate a default
  // instrument before delivering its saved component state. The old code
  // only staged the incoming state and silently kept the default machine.
  Instance late(factory,cid,48000);
  late.warmup();globals(late);expect(late,1,"omni");leave(late);
  saved_immediately_after_edit.pos=0;
  check(late.component->setState(&saved_immediately_after_edit)==kResultOk,"late active setState");
  wait_project(late,true);
  pump(late,30);
  globals(late);expect(late,11,"poly");leave(late);
  note(late,11,true);note(late,12,false);
 }
 {
  // A saved non-default HOST route is applied only when the late firmware
  // candidate is accepted; valid channel remap must work without a GUI poll.
  Stream host_routed;
  eiii::vst3::DecodedState routed_state;
  check(eiii::vst3::decode_state(saved_immediately_after_edit.bytes.data(),
       saved_immediately_after_edit.bytes.size(),routed_state),"decode routed test state");
  routed_state.value.midi_channel=11;
  check(eiii::vst3::encode_state(routed_state.value,host_routed.bytes),"encode routed test state");
  Instance routedLate(factory,cid,48000);
  routedLate.warmup();
  check(routedLate.receiver.state.forwarded_midi_channel==0,"new plugin must pass host MIDI");
  host_routed.pos=0;
  check(routedLate.component->setState(&host_routed)==kResultOk,"late routed setState");
  wait_project(routedLate,true);
  pump(routedLate,30);
  routedLate.poll();
  check(routedLate.receiver.state.forwarded_midi_channel==11,
        "accepted late state did not commit host channel 11");
  globals(routedLate);expect(routedLate,11,"poly");leave(routedLate);
  note(routedLate,1,true); // HOST channel 1 must be mapped to firmware 11.
 }
 {
  // Reject a foreign/corrupted D1 delta without accepting its sector writes.
  Stream poisoned;
  poisoned.bytes=saved_immediately_after_edit.bytes; // IBStream is noncopyable
  eiii::vst3::DecodedState poisonState;
  check(eiii::vst3::decode_state(poisoned.bytes.data(),poisoned.bytes.size(),poisonState),"decode poisoned state");
  auto& delta=poisonState.value.hd_changes[0];
  check(delta.size()>=28 && (delta.compare(0,8,"EIIIHDC1")==0 || delta.compare(0,8,"EIIIHDC2")==0),
        "no supported saved D1 change-set");
  delta[16]^=1; // source-image fingerprint mismatch in either supported format
  check(eiii::vst3::encode_state(poisonState.value,poisoned.bytes),"encode poisoned D1 state");
  Instance reject(factory,cid,48000);
  reject.warmup();globals(reject);expect(reject,1,"omni");leave(reject);
  reject.import(std::getenv("EIII_REPRO_WAV"));
  pump(reject,1);
  // Verify the *current* explicit HOST routing as well as the live bank:
  // a rejected saved state must not replace either with the failed candidate.
  routing(reject,7);
  reject.poll();
  check(reject.receiver.state.forwarded_midi_channel==7,
        "explicit host routing failed before rejected state");
  note(reject,1,true);
  poisoned.pos=0;
  check(reject.component->setState(&poisoned)==kResultOk,"late corrupt-but-framed VST3 state");
  wait_project(reject,false);
  pump(reject,25);
  reject.poll();
  check(std::string(reject.receiver.state.media_status).find("Project restore failed")!=std::string::npos,
        "wrong-disk restore was not rejected");
  globals(reject);expect(reject,1,"omni");leave(reject);
  reject.poll();
  check(reject.receiver.state.forwarded_midi_channel==7,
        "foreign D1 rejection incorrectly changed active HOST routing");
  note(reject,1,true); // same audible bank AND explicit routing must survive
 }
 {
  Instance recall(factory,cid,48000,&saved);recall.warmup();globals(recall);expect(recall,11,"poly");leave(recall);
  note(recall,11,true);note(recall,12,false);
  recall.mode(kRealtime,1024);recall.mode(kOffline,1024);globals(recall);expect(recall,11,"poly");leave(recall);
 }
 {
  // Two *simultaneously running* EIII instruments must have independent
  // firmware, writable D1 overlays, sample RAM and HOST MIDI remapping.
  // A uses the saved 11/poly bank. B creates a new bank from the same
  // read-only base OS image and deliberately chooses a different channel.
  Stream twinAState;
  Stream twinBState;
  {
   saved_immediately_after_edit.pos=0;
   Instance a(factory,cid,48000,&saved_immediately_after_edit);
   Instance b(factory,cid,48000);
   a.warmup();b.warmup();
   globals(a);expect(a,11,"poly");leave(a);
   globals(b);expect(b,1,"omni");leave(b);
   b.import(std::getenv("EIII_REPRO_WAV"));pump(b,1);
   custom(b,14,"poly");
   routing(b,14);
   b.poll();
   check(b.receiver.state.forwarded_midi_channel==14,"B host route changed");
   a.poll();
   check(a.receiver.state.forwarded_midi_channel==0,"B host route leaked into A");
   // Interleave GUI requests and playback on the two active worker threads.
   globals(a);expect(a,11,"poly");leave(a);
   globals(b);expect(b,14,"poly");leave(b);
   note(a,11,true);note(a,12,false);
   note(b,1,true); // HOST channel 1 is remapped to B firmware 14
   globals(a);expect(a,11,"poly");leave(a);
   a.save(twinAState);
   b.save(twinBState);
   check(!twinAState.bytes.empty()&&!twinBState.bytes.empty(),
         "independent twin instance snapshots missing");
  }
  {
   // Reopen both saved projects while both plugin instances are active.
   // A's firmware 11/poly and B's 14/poly + HOST remap 14 must persist.
   twinAState.pos=0;twinBState.pos=0;
   Instance a(factory,cid,48000,&twinAState);
   Instance b(factory,cid,48000,&twinBState);
   a.warmup();b.warmup();
   a.poll();b.poll();
   check(a.receiver.state.forwarded_midi_channel==0,"reloaded A HOST route contaminated");
   check(b.receiver.state.forwarded_midi_channel==14,"reloaded B HOST route lost");
   globals(a);expect(a,11,"poly");leave(a);
   globals(b);expect(b,14,"poly");leave(b);
   note(a,11,true);note(a,12,false);
   note(b,1,true);
   globals(a);expect(a,11,"poly");leave(a);
   globals(b);expect(b,14,"poly");leave(b);
  }
 }
 {
  Stream legacy;auto put=[&](uint32_t v){for(unsigned b=0;b<4;++b)legacy.bytes.push_back(uint8_t(v>>(b*8)));};put(0x33494945);put(1);put(7);
  Instance v1(factory,cid,48000,&legacy);v1.warmup();globals(v1);expect(v1,14,"poly");leave(v1);
  check(v1.receiver.state.forwarded_midi_channel==7,"v1 routing overwritten");
 }
 {
  Instance explicit_route(factory,cid,48000,nullptr,false);routing(explicit_route,7);explicit_route.mode(kOffline,1024);explicit_route.warmup();globals(explicit_route);expect(explicit_route,14,"poly");leave(explicit_route);
  check(explicit_route.receiver.state.forwarded_midi_channel==7,"explicit route overwritten");
 }
 std::cout<<"PASS firmware/VST3 MIDI defaults: new=1/omni; 16-channel audio; WAV; custom=11/poly power-cycle/project/mode recall; v1 and explicit routing; two concurrent EIII instances isolated and reloaded\n";return 0;
}
}
#ifdef EIII_OFFLINE_IN_PROCESS
int main(){try{const TUID cid{};return run_midi(nullptr,cid);}catch(const std::exception& e){std::cerr<<"FAIL MIDI defaults: "<<e.what()<<std::endl;return 1;}}
#elif !defined(EIII_OFFLINE_HOST_FACTORY_COMPILE_ONLY)
int wmain(int argc,wchar_t** argv){try{check(argc==2,"module path");const auto module=LoadLibraryW(argv[1]);check(module!=nullptr,"load VST3");const auto create=reinterpret_cast<IPluginFactory*(*)()>(GetProcAddress(module,"GetPluginFactory"));check(create!=nullptr,"factory export");auto* f=create();PClassInfo c{};check(f&&f->getClassInfo(0,&c)==kResultOk,"factory component");const auto result=run_midi(f,c.cid);f->release();FreeLibrary(module);return result;}catch(const std::exception& e){std::cerr<<"FAIL MIDI defaults: "<<e.what()<<std::endl;return 1;}}
#endif
