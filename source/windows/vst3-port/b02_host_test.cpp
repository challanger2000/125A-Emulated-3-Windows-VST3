// Real VST3 getState/setState and production worker, original ROM/OS/WAV only.
#ifdef EIII_OFFLINE_IN_PROCESS
#include "state_allocation_fault_test.h"
#endif
#define EIII_OFFLINE_HOST_SUPPORT_ONLY
#include "offline_host_test.cpp"
#include "project_state.h"
#include "core_section_container.h"
#include "core_machine_context.h"
#include "../../upstream/panel/eiii_panel_model.h"
#include "core_checkpoint_bundle.h"
using namespace offline_test;
namespace {
eiii::vst3::ProjectState decode(Stream const& stream){
 eiii::vst3::DecodedState state;
 check(eiii::vst3::decode_state(stream.bytes.data(),stream.bytes.size(),state),"decode host state");
 check(!state.value.machine_snapshot.empty(),"host state lacks machine snapshot");return std::move(state.value);
}
void encode(eiii::vst3::ProjectState const& state,Stream& stream){
 check(eiii::vst3::encode_state(state,stream.bytes),"encode modified host state");stream.pos=0;
}
std::vector<uint16_t> samples(eiii::vst3::ProjectState const& state){
 using namespace eiii::vst3;
 std::vector<CoreSectionContainer::Section> sections;
 check(CoreSectionContainer::decode(state.machine_snapshot.data(),state.machine_snapshot.size(),sections),"host machine container");
 std::vector<uint16_t> ram,snd;CoreMidiSection::State midi;CoreExecutionSection::State execution;
 check(CoreCheckpointBundle::decode(sections[0].bytes.data(),sections[0].bytes.size(),ram,snd,midi,execution),"host RAM checkpoint");
 return snd;
}
eiii::vst3::ProjectState foreign_rom(eiii::vst3::ProjectState state){
 using namespace eiii::vst3;
 std::vector<CoreSectionContainer::Section> sections;
 check(CoreSectionContainer::decode(state.machine_snapshot.data(),state.machine_snapshot.size(),sections),"foreign container");
 for(auto& section:sections)if(section.id==23){
  CoreMachineContext::State context;
  check(CoreMachineContext::decode(section.bytes.data(),section.bytes.size(),context),"foreign context");
  context.rom^=1;check(CoreMachineContext::encode(context,section.bytes),"foreign context encode");
 }
 check(CoreSectionContainer::encode(sections,state.machine_snapshot),"foreign container encode");return state;
}
void await_restore(Instance& instance,bool success){
 const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(30);
 for(;;){
  instance.poll();const std::string status=instance.receiver.state.media_status;
  if(status.find(success?"project restored":"restore failed")!=std::string::npos)return;
  check(std::chrono::steady_clock::now()<end,"worker project restore did not complete");
  // Test polling only. Production audio and emulated timing have no sleeps.
  std::this_thread::sleep_for(std::chrono::milliseconds(1));
 }
}
void audible(Instance& instance){
 auto render=instance.render(kOffline,257,.4,{{7,60,true},{uint64_t(instance.rate*.15)+7,60,false}});
 std::cout<<"B02 HOST audio rate="<<instance.rate<<" peak="<<render.peak<<" rms="<<render.rms<<std::endl;
 check(render.peak>.01&&render.rms>.001,"restored VST3 bank is silent");
 check(render.after.underrun_frames==render.before.underrun_frames,"offline recall audio underrun");
}
#ifdef EIII_OFFLINE_IN_PROCESS
void allocation_failures(Instance& instance,eiii::vst3::ProjectState const& state){
 Stream before;instance.save(before);
 for(bool restore:{false,true}){
  auto incoming=state;incoming.midi_channel=15;incoming.autoload_bank=43;
  Stream encoded;encode(incoming,encoded);bool completed=false;
  for(std::ptrdiff_t budget=0;budget<512;++budget){
   Stream output;encoded.pos=0;tresult result=kResultFalse;bool escaped=false;
   state_fault_test::budget=budget;
   try {result=restore?instance.component->setState(&encoded):instance.component->getState(&output);}
   catch(...) {escaped=true;}
   state_fault_test::budget=-1;
   check(!escaped,"allocation exception escaped VST3 state ABI");
   if(result==kResultOk){
    if(restore)await_restore(instance,true);
    std::cout<<"PASS B02 allocation sweep "<<(restore?"setState":"getState")<<" rejected_points="<<budget<<std::endl;
    check(budget>0,"allocation injection was not exercised");completed=true;break;
   }
   Stream retained;instance.save(retained);
   check(same_bank_project(before,retained),"state allocation failure changed committed project/bank/route");
  }
  check(completed,"state allocation sweep did not reach a complete successful call");
 }
}
#endif
#ifdef EIII_OFFLINE_IN_PROCESS
void unpolled_commit(Instance& instance,eiii::vst3::ProjectState state){
 auto accepted=state;accepted.midi_channel=6;accepted.autoload_bank=17;
 Stream good;encode(accepted,good);
 check(instance.component->setState(&good)==kResultOk,"queue unpolled valid project");
 auto* processor=static_cast<Processor*>(instance.component);
 const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);
 // Observe the actual owner commit without acknowledging its GUI result.
 // This is the no-editor/rapid successive setState lifecycle boundary.
 while(processor->midiRouting_.target()!=6){
  check(std::chrono::steady_clock::now()<deadline,"unpolled owner commit not observed");
  std::this_thread::sleep_for(std::chrono::milliseconds(1));
 }
 auto rejected=foreign_rom(state);rejected.midi_channel=16;rejected.autoload_bank=29;
 Stream bad;encode(rejected,bad);
 check(instance.component->setState(&bad)==kResultOk,"queue rejection after unacknowledged commit");
 await_restore(instance,false);Stream retained;instance.save(retained);const auto actual=decode(retained);
 std::cout<<"B02 unpolled commit retained autoload="<<actual.autoload_bank<<" expected=17 route="<<actual.midi_channel<<std::endl;
 check(same_bank_project(good,retained),"rejection after an unacknowledged commit restored stale project metadata");
 std::cout<<"PASS B02 committed-but-unpolled predecessor retained completely"<<std::endl;
}
#endif
void initial_midi_override(IPluginFactory* factory,const TUID cid,double rate,Stream& saved){
 Instance initial(factory,cid,rate,&saved);Stream v1;
 eiii::vst3::put32(v1.bytes,eiii::vst3::kStateMagic);eiii::vst3::put32(v1.bytes,1);eiii::vst3::put32(v1.bytes,9);
 check(initial.component->setState(&v1)==kResultOk,"v1 route during initial machine restore");
 await_restore(initial,true);Stream actual;initial.save(actual);
 const auto restored=decode(actual);
 check(restored.midi_channel==9&&samples(restored)==samples(decode(saved)),"initial v1 route lost the selected machine or saved route");
 audible(initial);
 std::cout<<"PASS B02 v1 override while initial full-machine restore is pending"<<std::endl;
}
void poweroff_media_restore(Instance& instance,eiii::vst3::ProjectState state){
 instance.poll();
 if(instance.receiver.state.running){
  HostMessage power;power.setMessageID("EIII.GUI.Control");
  power.getAttributes()->setInt("index",eiii::panel_find_control("power"));
  power.getAttributes()->setInt("down",1);
  check(instance.point->notify(&power)==kResultOk,"POWER OFF via actual panel message");
 }
 const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);
 do{instance.poll();check(std::chrono::steady_clock::now()<deadline,"POWER OFF not observed");
    if(!instance.receiver.state.running)break;std::this_thread::sleep_for(std::chrono::milliseconds(1));}while(true);
 state.machine_snapshot.clear();for(auto& delta:state.hd_changes)delta.clear();
 state.autoload_bank=-1;state.autoload_preset=-1;state.midi_channel=4;
 Stream blank;encode(state,blank);
 check(instance.component->setState(&blank)==kResultOk,"restore blank disk-overlay project while powered off");
 await_restore(instance,true);Stream saved;instance.save(saved);
 eiii::vst3::DecodedState decoded;
 check(eiii::vst3::decode_state(saved.bytes.data(),saved.bytes.size(),decoded),"decode powered-off project");
 std::cout<<"B02 POWER OFF saved D1 bytes="<<decoded.value.hd_changes[0].size()<<" expected=0"<<std::endl;
 check(same_bank_project(blank,saved),"powered-off restore resaved preceding project's HDD cache");
 std::cout<<"PASS B02 powered-off media/overlay transaction"<<std::endl;
}
#ifdef EIII_OFFLINE_IN_PROCESS
void powered_off_generations(){
 eiii::engine engine;eiii::engine_config first;first.realtime=false;
 first.rt.rom_dir=std::getenv("EIII_ROM_DIR");first.rt.hd_path=std::getenv("EIII_HD_IMAGE");
 engine.power(false);engine.start(first);engine.invalidate_project_restores(1);
 std::atomic<int> callback{0};
 check(engine.restore_project(first,[&]() noexcept{callback=1;},1),"queue powered-off predecessor");
 const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);
 while(!engine.project_restore_preparing()){
  check(std::chrono::steady_clock::now()<deadline,"powered-off preparation not observed");std::this_thread::yield();
 }
 engine.invalidate_project_restores(2);auto second=first;second.rt.cem_model="v1";
 check(engine.restore_project(second,[&]() noexcept{callback=2;},2),"queue powered-off successor");
 bool oldRejected=false,newAccepted=false;unsigned received=0;
 while(received<2){
  eiii::media_result result;
  if(engine.poll_media_result(result)){
   ++received;if(result.project_generation==1)oldRejected=!result.ok;
   if(result.project_generation==2)newAccepted=result.ok;
  }else std::this_thread::yield();
  check(std::chrono::steady_clock::now()<deadline,"powered-off generation result timeout");
 }
 check(oldRejected&&newAccepted&&callback==2,"superseded powered-off candidate committed");
 std::cout<<"PASS B02 powered-off stale prepared candidate rejected"<<std::endl;
}
#endif
void run_rate(IPluginFactory* factory,const TUID cid,double rate,const std::string& wave){
 Stream saved;
 {
  Instance source(factory,cid,rate);source.warmup();source.import(wave.c_str());
  audible(source);source.save(saved);
 }
 const auto state=decode(saved);const auto expectedSamples=samples(state);
 initial_midi_override(factory,cid,rate,saved);
 {
  // A volatile bank needs no disk autoload. Render immediately after activation:
  // the worker must validate the initial full machine before consuming MIDI.
  auto volatileBank=state;volatileBank.autoload_bank=-1;volatileBank.autoload_preset=-1;
  Stream initial;encode(volatileBank,initial);Instance direct(factory,cid,rate,&initial);
  audible(direct);await_restore(direct,true);Stream captured;direct.save(captured);
  check(samples(decode(captured))==expectedSamples,"initial no-autoload restore changed sample RAM");
 }
 {
  Stream incompatible;encode(foreign_rom(state),incompatible);
  Instance initialFailure(factory,cid,rate,&incompatible);await_restore(initialFailure,false);
  Stream unsafe;check(initialFailure.component->getState(&unsafe)==kResultFalse,"failed initial restore allowed destructive save");
  saved.pos=0;check(initialFailure.component->setState(&saved)==kResultOk,"recover failed initial restore");
  await_restore(initialFailure,true);audible(initialFailure);
 }
 {
  // Project reopening: restore before activation, then reactivation/mode changes.
  Instance reopened(factory,cid,rate,&saved);await_restore(reopened,true);
  Stream immediate;reopened.save(immediate);
  check(samples(decode(immediate))==expectedSamples,"reopen changed full sample RAM");
  audible(reopened);reopened.mode(kRealtime,512);reopened.mode(kOffline,1024);
  audible(reopened);
  // Repeat late state recall on the same running processor.
  for(int repeat=0;repeat<2;++repeat){
   saved.pos=0;check(reopened.component->setState(&saved)==kResultOk,"late setState");
   await_restore(reopened,true);Stream again;reopened.save(again);
   check(samples(decode(again))==expectedSamples,"late recall changed full sample RAM");
   audible(reopened);
  }
#ifdef EIII_OFFLINE_IN_PROCESS
  allocation_failures(reopened,state);
  unpolled_commit(reopened,state);
#endif
  // Invalid nested checksum is rejected synchronously before staging metadata.
  auto damaged=state;damaged.machine_snapshot.back()^=1;Stream bad;encode(damaged,bad);
  check(reopened.component->setState(&bad)==kResultFalse,"accepted damaged machine checksum");
  Stream retained;reopened.save(retained);
  check(samples(decode(retained))==expectedSamples,"checksum rejection changed bank");
  // Valid wrappers carrying a foreign ROM identity reject on the worker.
  using namespace eiii::vst3;
  auto foreign=foreign_rom(state);
  foreign.midi_channel=16;encode(foreign,bad);
  reopened.mode(kRealtime,1024);
  const auto oldRoute=reopened.receiver.state.forwarded_midi_channel;
  check(reopened.component->setState(&bad)==kResultOk,"queue incompatible candidate");
  const auto concurrent=reopened.render(kRealtime,257,1.5,
    {{7,60,true},{uint64_t(rate*.15)+7,60,false}},true);
  std::cout<<"B02 HOST rejected-restore RT underrun_frames="
           <<concurrent.after.underrun_frames-concurrent.before.underrun_frames<<std::endl;
  check(concurrent.after.underrun_frames==concurrent.before.underrun_frames,
        "candidate validation interrupted the retained realtime machine");
  await_restore(reopened,false);reopened.poll();
  reopened.mode(kOffline,1024);
  check(reopened.receiver.state.forwarded_midi_channel==oldRoute,"rejected restore changed host routing");
  reopened.save(retained);check(samples(decode(retained))==expectedSamples,"rejected restore changed live sample RAM");
  audible(reopened);
  // A larger realtime buffer used to restart the engine and reuse a stale
  // checkpoint; preserve the running bank and grow lookahead in the fixed ring.
  reopened.poll();const auto starts=reopened.receiver.state.engine_starts;
  reopened.mode(kRealtime,4096);reopened.poll();
  check(reopened.receiver.state.engine_starts==starts,"large buffer change restarted the saved machine");
  reopened.mode(kOffline,1024);Stream resized;reopened.save(resized);
  check(samples(decode(resized))==expectedSamples,"large buffer change lost volatile sample RAM");
  audible(reopened);
  // A queued full restore superseded by a legacy MIDI-only state must not
  // leave the processor pending forever or overwrite the user's latest route.
  saved.pos=0;check(reopened.component->setState(&saved)==kResultOk,"queue superseded restore");
  Stream v1;put32(v1.bytes,kStateMagic);put32(v1.bytes,1);put32(v1.bytes,9);
  check(reopened.component->setState(&v1)==kResultOk,"v1 supersedes full restore");
  reopened.render(kOffline,512,.2);reopened.poll();
  check(reopened.receiver.state.forwarded_midi_channel==9,"stale restore replaced legacy MIDI route");
  reopened.save(retained);
#ifdef EIII_OFFLINE_IN_PROCESS
  static_cast<Processor*>(reopened.component)->engine_.power(false);
  Stream poweredOff;reopened.save(poweredOff);
  eiii::vst3::DecodedState off;
  check(eiii::vst3::decode_state(poweredOff.bytes.data(),poweredOff.bytes.size(),off)&&off.value.machine_snapshot.empty(),
        "POWER OFF retained a stale machine snapshot");
#endif
  poweroff_media_restore(reopened,state);
 }
#ifdef EIII_OFFLINE_IN_PROCESS
 {
  const auto start=std::chrono::steady_clock::now();
  {
   Instance closing(factory,cid,rate,&saved);await_restore(closing,true);
   saved.pos=0;check(closing.component->setState(&saved)==kResultOk,"queue close-during-prepare state");
   auto* processor=static_cast<Processor*>(closing.component);
   const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);
   while(!processor->engine_.project_restore_preparing()){
    check(std::chrono::steady_clock::now()<deadline,"detached preparation was not observed");std::this_thread::yield();
   }
   std::cout<<"B02 HOST closing with an observed in-flight preparation"<<std::endl;
  }
  check(std::chrono::steady_clock::now()-start<std::chrono::seconds(30),"shutdown deadlocked during candidate preparation");
 }
#endif
 // V2/V3 migration still uses firmware/media cold boot without a machine block.
 for(unsigned version:{2u,3u}){
  Stream legacy;auto media=state;media.machine_snapshot.clear();encode(media,legacy);
  legacy.bytes.resize(legacy.bytes.size()-(version==2?12:4));legacy.bytes[4]=uint8_t(version);
  Instance old(factory,cid,rate,&legacy);old.warmup();
  if(version==3)audible(old); // v2 predates firmware bank autoload by definition
  Stream migrated;old.save(migrated);check(decode(migrated).hd_path==state.hd_path,"legacy D1 path lost");
 }
 std::cout<<"PASS B02 VST3 rate="<<rate<<" before/after activation, reopen, repeated, RT/offline, negative, v1/v2/v3"<<std::endl;
}
std::string synthetic_wave(std::filesystem::path& owned){
 if(const char* path=std::getenv("EIII_REPRO_WAV"))return path;
 owned=std::filesystem::temp_directory_path()/("125a-b02-host-"+
   std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".wav");
 std::vector<uint8_t> bytes;
 auto tag=[&](const char* s){bytes.insert(bytes.end(),s,s+4);};
 auto word=[&](uint16_t x){bytes.push_back(uint8_t(x));bytes.push_back(uint8_t(x>>8));};
 const uint32_t frames=22050,dataBytes=frames*4;
 tag("RIFF");eiii::vst3::put32(bytes,36+dataBytes);tag("WAVE");tag("fmt ");
 eiii::vst3::put32(bytes,16);word(1);word(2);eiii::vst3::put32(bytes,44100);
 eiii::vst3::put32(bytes,44100*4);word(4);word(16);tag("data");eiii::vst3::put32(bytes,dataBytes);
 for(uint32_t i=0;i<frames;++i)for(unsigned channel=0;channel<2;++channel)
  word(uint16_t(int16_t(12000*std::sin(6.283185307179586*(channel?1500:1000)*i/44100.))));
 std::ofstream file(owned,std::ios::binary);file.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());
 check(file.good(),"synthetic host WAV creation failed");return owned.string();
}
int run_b02(IPluginFactory* factory,const TUID cid){
 if(!std::getenv("EIII_ROM_DIR")||!std::getenv("EIII_HD_IMAGE")){
  std::cout<<"NOT RUN B02 VST3: original ROM/953-MB OS required\n";return 77;
 }
 std::filesystem::path owned;
 struct Cleanup{std::filesystem::path& p;~Cleanup(){if(!p.empty()){std::error_code ec;std::filesystem::remove(p,ec);}}} cleanup{owned};
 const auto wave=synthetic_wave(owned);
 for(double rate:{44100.,48000.})run_rate(factory,cid,rate,wave);
#ifdef EIII_OFFLINE_IN_PROCESS
 powered_off_generations();
#endif
 return 0;
}
}
#ifdef EIII_OFFLINE_IN_PROCESS
int main(){try{const TUID cid{};return run_b02(nullptr,cid);}catch(std::exception const& e){std::cerr<<"FAIL B02 VST3: "<<e.what()<<std::endl;return 1;}}
#else
int wmain(int argc,wchar_t** argv){try{
 check(argc==2,"module path");auto module=LoadLibraryW(argv[1]);check(module!=nullptr,"load VST3");
 auto create=reinterpret_cast<IPluginFactory*(*)()>(GetProcAddress(module,"GetPluginFactory"));check(create!=nullptr,"factory export");
 auto* factory=create();PClassInfo info{};check(factory&&factory->getClassInfo(0,&info)==kResultOk,"factory component");
 auto result=run_b02(factory,info.cid);factory->release();FreeLibrary(module);return result;
 }catch(std::exception const& e){std::cerr<<"FAIL B02 VST3: "<<e.what()<<std::endl;return 1;}}
#endif
