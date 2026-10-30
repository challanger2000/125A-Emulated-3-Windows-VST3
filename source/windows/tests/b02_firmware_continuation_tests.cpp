// Production B02 end-to-end gate. Original ROM/OS stay external and read-only.
// Missing fixtures are NOT RUN (77), never PASS. Synthetic WAV is firmware-imported.
#include "../../upstream/runtime/eiii_runtime_impl.h"
#include "../../upstream/runtime/wav_firmware_import.h"
#include "../../upstream/runtime/eiii_engine.h"
#include <chrono>
#include <fstream>
#include <thread>
#include "../../upstream/panel/eiii_lcd.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
using Core=eiii::runtime::impl;
namespace {
void require(bool ok,const std::string& why){if(!ok)throw std::runtime_error(why);}
std::vector<uint8_t> capture(eiii::runtime& rt){
 rt.bind_thread();std::vector<uint8_t> bytes;
 require(rt.capture_device_sections(bytes),"production snapshot capture");return bytes;
}
void identical(eiii::runtime& a,eiii::runtime& b,const char* label){
 auto left=capture(a),right=capture(b);
 if(left!=right){
  std::vector<eiii::vst3::CoreSectionContainer::Section> x,y;
  eiii::vst3::CoreSectionContainer::decode(left.data(),left.size(),x);
  eiii::vst3::CoreSectionContainer::decode(right.data(),right.size(),y);
  for(size_t i=0;i<std::min(x.size(),y.size());++i)
   if(x[i].id!=y[i].id||x[i].bytes!=y[i].bytes)
    std::cerr<<"B02 mismatch: "<<label<<" section="<<x[i].id<<"\n";
  throw std::runtime_error(std::string(label)+": complete serialized state differs");
 }
}
void advance(eiii::runtime& rt,double seconds){
 rt.bind_thread();const auto end=rt.cycles()+uint64_t(seconds*rt.cpu_hz);
 while(rt.cycles()<end){
  auto before=rt.cycles();rt.run_until_cycle(std::min(end,before+10000));
  rt.render_audio();rt.audio().clear();
  require(!rt.stop_code()&&rt.cycles()>before,"firmware stopped or clock stuck");
 }
}
void boot(eiii::runtime& rt,eiii::runtime& peer,eiii::runtime_config& cfg){
 bool transfer_checked=false;
 for(unsigned ms=0;ms<60000;++ms){
  eiii::panel_snapshot panel{};rt.snapshot(panel);
  auto line=eiii::lcd_line_text(panel.lcd,0);
  if(line.size()>3&&line[0]=='P'&&line[3]==' ') {
   require(transfer_checked,"no in-flight original-firmware SCSI checkpoint observed");return;
  }
  advance(rt,.001);
  ncr5380_device::recall_state ncr{};
  if(!transfer_checked && rt.internals().hdc->capture_recall_state(ncr) && ncr.phase!=0) {
   auto bytes=capture(rt);std::string error;
   for(unsigned id=1;id<8;++id)if(rt.drive_kind(id)==eiii::scsi_kind::hd)
    cfg.hd_changes[id]=rt.hd_changed_sectors(id)?rt.hd_changes(id):std::string{};
   {bool ok=peer.open_restored(cfg,bytes.data(),bytes.size(),error);require(ok,"SCSI-phase restore: "+error);}
   identical(rt,peer,"in-flight firmware SCSI transaction");
   auto end=rt.cycles()+100000;
   rt.bind_thread();rt.run_until_cycle(end);rt.render_audio();
   peer.bind_thread();peer.run_until_cycle(end);peer.render_audio();
   identical(rt,peer,"in-flight SCSI continuation");rt.audio().clear();peer.audio().clear();
   rt.bind_thread();transfer_checked=true;
   std::cout<<"B02 original-firmware SCSI phase="<<ncr.phase<<" continued exactly"<<std::endl;
  }
 }
 throw std::runtime_error("original firmware main screen timeout");
}
eiii::wav::Audio audio_fixture(){
 eiii::wav::Audio audio;audio.rate=44100;audio.channels=2;audio.pcm.resize(22050);
 for(size_t i=0;i<audio.frames();++i)for(unsigned c=0;c<2;++c)
  audio.pcm[2*i+c]=float(.4*std::sin(6.283185307179586*(c?1500:1000)*double(i)/audio.rate));
 return audio;
}
void overlays(eiii::runtime& rt,eiii::runtime_config& cfg){
 for(unsigned id=1;id<8;++id)if(rt.drive_kind(id)==eiii::scsi_kind::hd)
  cfg.hd_changes[id]=rt.hd_changed_sectors(id)?rt.hd_changes(id):std::string{};
}
void continuation(eiii::runtime& a,eiii::runtime& b,bool audible){
 const auto start=a.cycles();double peak=0;size_t frames=0;
 // Include UART half-bit crossings, SCC receive completion, timer/IRQ/SCSI
 // advancement and several audio blocks, not merely matching initial RAM.
 for(uint64_t step:{1ull,128ull,256ull,512ull,1024ull,10000ull,100000ull,500000ull}){
  a.bind_thread();a.run_until_cycle(start+step);a.render_audio();const auto aa=a.audio();
  b.bind_thread();b.run_until_cycle(start+step);b.render_audio();const auto bb=b.audio();
  require(!a.stop_code()&&!b.stop_code(),"continued machine halted");
  require(aa==bb,"sample-exact stereo audio continuation");
  for(float x:aa){require(std::isfinite(x),"non-finite restored audio");peak=std::max(peak,double(std::abs(x)));}
  frames+=aa.size()/2;
  identical(a,b,"CPU/timer/RAM/MIDI/SCSI/audio continuation");
  a.audio().clear();b.audio().clear();
 }
 if(audible)require(peak>.001,"active-voice continuation is silent");
 std::cout<<"B02 continuation frames="<<frames<<" peak="<<peak<<" audible="<<audible<<std::endl;
}
std::vector<uint8_t> corrupt_section(std::vector<uint8_t> const& saved,uint32_t id){
 std::vector<eiii::vst3::CoreSectionContainer::Section> sections;
 require(eiii::vst3::CoreSectionContainer::decode(saved.data(),saved.size(),sections),"decode negative fixture");
 for(auto& section:sections)if(section.id==id){
  std::vector<uint8_t> payload;
  require(eiii::vst3::CoreStateEnvelope::decode(section.bytes.data(),section.bytes.size(),payload),"decode negative subsection");
  // Keep valid envelopes/checksums; invalidate the device-specific schema tag.
  payload[0]^=0xff;
  require(eiii::vst3::CoreStateEnvelope::encode(payload,section.bytes),"encode negative subsection");
 }
 std::vector<uint8_t> out;require(eiii::vst3::CoreSectionContainer::encode(sections,out),"encode negative container");return out;
}
void rejection(eiii::runtime& destination,std::vector<uint8_t> const& bad,const char* label){
 const auto before=capture(destination);
 require(!destination.restore_device_sections(bad.data(),bad.size()),std::string("accepted ")+label);
 require(capture(destination)==before,std::string("rejected restore altered live destination: ")+label);
 require(!destination.stop_code(),"rejected restore stopped destination");
}
void run(eiii::runtime_config cfg){
 std::string error;eiii::runtime a,b;
 require(a.open(cfg,error),"source open: "+error);a.run_until_cycle(1500000);
 auto saved=capture(a);
 // Regression for enabled timers parked at never by the original serial code.
 auto raw=std::make_unique<Core::core_recall_state>();require(a.internals().capture_core_recall(*raw),"raw capture");
 require(std::any_of(raw->scheduler.timers.begin(),raw->scheduler.timers.end(),[](auto const& t){return t.enabled&&t.expire.is_never();}),"missing parked-enabled timer regression precondition");
 {bool ok=b.open_restored(cfg,saved.data(),saved.size(),error);require(ok,"production snapshot restore: "+error);}
 identical(a,b,"early boot immediate restore");continuation(a,b,false);
 boot(a,b,cfg);overlays(a,cfg);
 std::atomic<bool> cancel=false;eiii::wav::Result imported;
 auto wave=audio_fixture();
 require(eiii::wav::execute(a,cfg,wave,{60,36,84,0},cancel,imported,error),"original firmware WAV import: "+error);
 require(bool(imported.machine),"no committed firmware sample bank");
 eiii::runtime& source=*imported.machine;cfg=imported.config;source.bind_thread();
 // Checkpoint before the first serial byte is complete; preserve shift state.
 const uint8_t on[]={0x9d,60,100};source.midi_in(on,3);advance(source,.00016);
 saved=capture(source);overlays(source,cfg);
 {bool ok=b.open_restored(cfg,saved.data(),saved.size(),error);require(ok,"in-flight UART restore: "+error);}
 identical(source,b,"in-flight UART immediate restore");continuation(source,b,true);
 // Snapshot active voices again and repeatedly reopen from the portable bytes.
 // A bank reload here would destroy the exact voice/RAM/FIR/CEM continuation.
 const uint8_t off[]={0x8d,60,0};source.bind_thread();source.midi_in(off,3);advance(source,.00016);
 for(unsigned repeat=0;repeat<3;++repeat){
  saved=capture(source);overlays(source,cfg);
  {bool ok=b.open_restored(cfg,saved.data(),saved.size(),error);require(ok,"repeated restore: "+error);}
  identical(source,b,"repeated immediate restore");continuation(source,b,false);
 }
 auto truncated=saved;truncated.pop_back();rejection(b,truncated,"truncated container");
 auto damaged=saved;damaged.back()^=1;rejection(b,damaged,"checksum mismatch");
 // Syntactically valid wrappers with incompatible device data must also reject.
 for(uint32_t id:{2u,3u,4u,8u,20u,22u,23u,34u})
  rejection(b,corrupt_section(saved,id),"valid envelope / incompatible device section");
 // Same current images with a foreign ROM or overlay identity cannot match.
 auto foreign=std::make_unique<Core::core_recall_state>();
 source.bind_thread();require(source.internals().capture_core_recall(*foreign),"foreign capture");
 std::vector<uint8_t> bytes;
 ++foreign->audio.output_frames;
 require(Core::encode_core_device_sections(*foreign,bytes),"inconsistent audio count encode");
 rejection(b,bytes,"inconsistent audio frame counter / model clock");
 --foreign->audio.output_frames;
 foreign->context.rom^=1;
 require(Core::encode_core_device_sections(*foreign,bytes),"foreign ROM encode");rejection(b,bytes,"wrong ROM identity");
 foreign->context.rom^=1;foreign->context.overlays[1]^=1;
 require(Core::encode_core_device_sections(*foreign,bytes),"foreign overlay encode");rejection(b,bytes,"wrong overlay identity");
 // An optional target changes timer/device topology and must fail closed.
 auto wrong=cfg;wrong.scsi[4]={eiii::scsi_kind::cd,{}};
 const auto before=capture(b);
 require(!b.open_restored(wrong,saved.data(),saved.size(),error),"accepted foreign SCSI topology");
 require(capture(b)==before,"wrong-topology restore altered live machine");
 std::cout<<"PASS B02 firmware production snapshot rate="<<cfg.sample_rate<<" model="<<cfg.cem_model
          <<" bytes="<<saved.size()<<" immediate/full continuation/repeated/negative"<<std::endl;
}
void atomic_media(eiii::runtime_config cfg){
 auto path=std::filesystem::temp_directory_path()/("125a-b02-media-"+
   std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".iso");
 struct Cleanup{std::filesystem::path p;~Cleanup(){std::error_code ec;std::filesystem::remove(p,ec);}} cleanup{path};
 {std::vector<char> data(128*1024,0);std::ofstream f(path,std::ios::binary);f.write(data.data(),data.size());require(f.good(),"synthetic ISO write");}
 eiii::engine engine;eiii::engine_config launch;launch.rt=cfg;engine.start(launch);
 const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);
 eiii::panel_snapshot panel{};
 while(!panel.running){engine.snapshot(panel);require(std::chrono::steady_clock::now()<deadline,"engine boot for atomic-media test");std::this_thread::yield();}
 require(engine.set_media(eiii::media_slot::cd,path.string()),"queue CD mount");
 require(engine.set_media(eiii::media_slot::cd,{}),"queue CD eject");
 require(engine.set_media(eiii::media_slot::cd,path.string()),"queue CD remount");
 std::vector<uint8_t> bytes;eiii::runtime_config actual;
 require(engine.capture_project(bytes,actual),"atomic project capture after pending media change");
 require(engine.media_path(eiii::media_slot::cd).empty(),"GUI view must still be stale before result polling");
 require(actual.cd_path==path.string(),"snapshot contains stale CD path");
 engine.shutdown();
 eiii::runtime restored;std::string error;
 {bool ok=restored.open_restored(actual,bytes.data(),bytes.size(),error);require(ok,"CD remount generation restore: "+error);}
 require(capture(restored)==bytes,"atomic metadata/CD snapshot differs immediately after restore");
 std::cout<<"PASS B02 atomic media/snapshot capture, stale GUI view, repeated CD generation and pending media change"<<std::endl;
 eiii::engine setup;launch.first_start_midi_omni=true;setup.start(launch);
 const auto setup_deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);
 while(!setup.firmware_setup_busy()) {
  require(std::chrono::steady_clock::now()<setup_deadline,"firmware setup precondition not observed");std::this_thread::yield();
 }
 std::vector<uint8_t> unchanged{0x42};eiii::runtime_config untouched;untouched.hd_path="unchanged";
 require(!setup.capture_project(unchanged,untouched) && unchanged==std::vector<uint8_t>{0x42} && untouched.hd_path=="unchanged",
         "incomplete firmware automation was published as a complete project snapshot");
 setup.shutdown();
 std::cout<<"PASS B02 uncommitted firmware-automation capture rejected without output mutation"<<std::endl;
}
}
int main(int argc,char** argv){
 const char* rom=std::getenv("EIII_B02_ROM_DIR"),*hd=std::getenv("EIII_B02_HD_IMAGE");
 if(argc==3){rom=argv[1];hd=argv[2];}
 if(argc!=1&&argc!=3){std::cerr<<"Usage: B02_TEST [ROM_FOLDER OS_IMAGE]\n";return 9;}
 if(!rom||!*rom||!hd||!*hd){std::cout<<"NOT RUN B02: original ROMs and EIIIOS2_42_HDImage_953MB.ISO required\n";return 77;}
 try{
  require(std::filesystem::path(hd).filename()=="EIIIOS2_42_HDImage_953MB.ISO","use the actual 953-MB OS fixture");
  for(double rate:{44100.,48000.})for(auto model:{"v1","v2"}){
   eiii::runtime_config cfg;cfg.rom_dir=rom;cfg.hd_path=hd;cfg.sample_rate=rate;cfg.cem_model=model;run(cfg);
  }
  eiii::runtime_config cfg;cfg.rom_dir=rom;cfg.hd_path=hd;atomic_media(cfg);
  std::cout<<"PASS B02 original-firmware runtime gate (not Studio One acceptance)\n";return 0;
 }catch(std::exception const& e){std::cerr<<"FAIL B02: "<<e.what()<<std::endl;return 1;}
}
