#include "midi_controllers.h"
#include "midi_dispatch.h"
#include "disk_export_snapshot.h"
#include "export_worker_guard.h"
#ifdef _WIN32
#include "headless_dirty_pump_win.h"
#endif
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "restore_revisions.h"
#include "project_activation.h"
#include "overlay_restore_guard.h"
// Native VST3 bridge to threaded EIII engine; requires user-provided original ROM and OS image.
#include "public.sdk/source/vst/vstaudioeffect.h"
#include "public.sdk/source/vst/vsteditcontroller.h"
#include "public.sdk/source/main/pluginfactory.h"
#include "pluginterfaces/base/ustring.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "../../upstream/runtime/eiii_engine.h"
#include "../../upstream/standalone/first_start_midi.h"
#include "eiii_editor_win.h"
#include "eiii_zoom_state.h"
#include "../../upstream/standalone/shim/utf8_file_paths.h"
#include "midi_routing.h"
#include "project_dirty_revision.h"
#include "note_off_recovery.h"
#include "../../upstream/runtime/latency_policy.h"
#include "project_state.h"
#include "core_section_roster.h"
#include "core_d0_format.h"
#include "sample_input.h"
#include "pluginterfaces/base/ibstream.h"
#include "panel/eiii_panel_model.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <optional>
#include <type_traits>
#include <string>
#include <algorithm>
#include <cstdlib>
#include <vector>
#include <sstream>
#include <filesystem>
#include <array>
#include <thread>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
using namespace Steinberg;
using namespace Steinberg::Vst;
namespace {
// Optional private firmware bundle alongside the VST3 module.
// Contents/Resources/ROM and Contents/Resources/OS are populated only in
// user-local packages. No proprietary firmware is committed to the repository.
static std::filesystem::path bundled_resources() {
#ifdef _WIN32
 HMODULE module=nullptr;
 if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                         GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                         reinterpret_cast<LPCWSTR>(&bundled_resources), &module) || !module)
  return {};
 std::wstring buffer(32768, L'\0');
 const DWORD count=GetModuleFileNameW(module,buffer.data(),DWORD(buffer.size()));
 if (!count || count>=buffer.size()) return {};
 buffer.resize(count);
 const auto bin=std::filesystem::path(buffer);
 return bin.parent_path().parent_path()/"Resources";
#else
 return {};
#endif
}
static std::string native_path_utf8(const std::filesystem::path& p) {
 return eiii::shim::utf8_from_path(p);
}
static void apply_bundled_firmware(eiii::engine_config& cfg) {
 namespace fs=std::filesystem;
 const auto resources=bundled_resources();
 if (resources.empty()) return;
 std::error_code ec;
 const auto rom=resources/"ROM";
 const auto large_os=resources/"OS"/"EIIIOS2_42_HDImage_953MB.ISO";
 const auto fallback_os=resources/"OS"/"EIII_OS242_UST_TEST_96MB.ISO";
 const auto os=fs::is_regular_file(large_os,ec)?large_os:fallback_os;
 const bool rom_ok=fs::is_regular_file(rom/"e3-lsboot_ip381a_emu_systems_4088.ic3",ec) &&
   fs::is_regular_file(rom/"e3-msboot_ip380a_emu_systems_4088.ic4",ec);
 if(cfg.rt.rom_dir.empty() && rom_ok) cfg.rt.rom_dir=native_path_utf8(rom);
 if(cfg.rt.hd_path.empty() && fs::is_regular_file(os,ec))
  cfg.rt.hd_path=native_path_utf8(os);
}
#if defined(EIII_125A_PUBLIC_VARIANT)
// A dedicated identity: do not reuse the private EIII class IDs.
static const FUID kProcessor(0xA125A031,0x696E4BA4,0x9E36D501,0x71250201);
static const FUID kController(0xA125A031,0x696E4BA4,0x9E36D501,0x71250202);
#else
// Preserve the previously shipped test instrument's VST3 identity and song recall.
static const FUID kProcessor(0xB3C14177,0x63904397,0xA671B5CB,0x0364AB10);
static const FUID kController(0xB3C14177,0x63904397,0xA671B5CB,0x0364AB11);
#endif
class Processor final : public AudioEffect {
public:
 Processor(){ setControllerClass(kController); }
 ~Processor() override {
#ifdef _WIN32
  headlessDirtyPump_.stop();
#endif
  engine_.shutdown(); if(exportThread_.joinable()) exportThread_.join();
 }
 eiii::engine engine_;
 std::atomic<uint32_t> midiNoteOns_{0};
 std::atomic<uint32_t> lastMidiChannel_{0};
 std::atomic<uint32_t> outputPeakMilli_{0};
 std::atomic<int32> lastProcessMode_{-1};
 int32 setupMode_ = kRealtime;
 uint32 engineStarts_ = 0;
 std::array<std::atomic<uint64_t>,3> processFrames_{};
 eiii::vst3::NoteOffRecovery noteOffRecovery_;
 struct MidiPoint { int32 offset; uint32 order; bool controller; uint32 id; ParamValue value; Event event; };
 std::array<MidiPoint,8192> midiTimeline_{}; // preallocated; no audio-thread stack spike
 void retryNoteOffs() noexcept {
  noteOffRecovery_.retry([this](const uint8_t* bytes) noexcept {return engine_.midi(0,bytes,3);});
 }
 bool deferNoteOff(const uint8_t* bytes) noexcept {return noteOffRecovery_.defer(bytes);}
 eiii::vst3::MidiRouting midiRouting_; // 0: host channel, 1..16: user-selected EIII channel
 bool savedMidiRouting_ = false; // valid host project state or explicit GUI selection
 bool stagedMidiRouting_ = false; // late host restore pending acceptance or engine reactivation
 unsigned stagedMidiTarget_ = 0;
 std::string mediaStatus_; // GUI-thread only: never used from process()
 static_assert(std::is_nothrow_move_assignable_v<eiii::vst3::ProjectState>);
 eiii::vst3::ProjectState project_;
 std::optional<eiii::vst3::ProjectState> previousProject_;
 bool previousAutoloadExplicit_=false;
 uint64_t projectGeneration_=0;
 bool initialMachinePending_=false;
 uint64_t initialMachineGeneration_=0;
 bool previousProjectLoaded_=false,previousSavedMidiRouting_=false;
 uint64_t previousRouteRevision_=0;
 bool projectLoaded_ = false;
 uint32_t wavSerial_ = 0;
 eiii::vst3::ProjectDirtyRevision dirtyRevision_;
 uint64_t lastFirmwareHdSerial_ = 0;
 uint64_t lastAckedDirty_ = 0; // acknowledged by connected controller on UI thread
#ifdef _WIN32
 eiii::vst3::HeadlessDirtyPump headlessDirtyPump_;
#endif

 // Only non-audio host/GUI callbacks may issue VST3 component->controller
 // messages. The worker increments an atomic counter; pollMedia bridges it.
 void sendDirtyRevision(uint64_t revision) {
  if(!revision || revision<=lastAckedDirty_) return;
  IMessage* message=allocateMessage();
  if(!message) return;
  message->setMessageID("EIII.GUI.Dirty");
  if(message->getAttributes())
   message->getAttributes()->setInt("revision",int64(revision));
  sendMessage(message);
  message->release();
 }
 void notifyProjectDirty() {
  sendDirtyRevision(dirtyRevision_.changed());
 }
 void headlessDirtyTick() {
  // Hosted by a Win32 message-only HWND on the owning UI thread, even
  // when NO plugin view exists. No work or host callbacks in process().
  pollMedia();
  // Early connection/handler failures are retried until controller ACK.
  sendDirtyRevision(dirtyRevision_.current());
 }
 eiii::vst3::ProjectActivation activation_;
 eiii::vst3::OverlayRestoreGuard overlayGuard_;
 eiii::vst3::RestoreRevisions restoreRevisions_;
 bool autoloadExplicit_ = false; // v3 host state or UI selection takes precedence over test script
 std::thread exportThread_; // file I/O is never performed in process()
 std::atomic<bool> exportFinished_{false};
 std::string exportResult_; // published by release/acquire flag; GUI thread only

 bool rollbackPendingProject() noexcept {
  if(!previousProject_)return false;
  project_=std::move(*previousProject_);previousProject_.reset();
  autoloadExplicit_=previousAutoloadExplicit_;projectLoaded_=previousProjectLoaded_;
  if(restoreRevisions_.route_revision()==previousRouteRevision_)savedMidiRouting_=previousSavedMidiRouting_;
  activation_.activated();stagedMidiRouting_=false;overlayGuard_.new_project();
  return true;
 }
 // Called from non-audio plugin callbacks only.
 void pollMedia()
 {
  if(exportFinished_.exchange(false,std::memory_order_acquire)) {
   if(exportThread_.joinable()) exportThread_.join();
   mediaStatus_=exportResult_;
  }
  if(initialMachinePending_) {
   std::string error;const int result=engine_.initial_restore_result(error);
   if(result>=2) {
    initialMachinePending_=false;
    if(initialMachineGeneration_==projectGeneration_) {
     if(result==2) {activation_.activated();mediaStatus_="Saved Studio One project restored (full machine)";}
     else {overlayGuard_.restore_warning(true);mediaStatus_="Project restore failed: "+error;}
    }
   }
  }
  eiii::media_result media{};
  while (engine_.poll_media_result(media)) {
   if (media.slot==eiii::media_slot::project_restore) {
    // Only the newest setState() may change the save guard, activation state
    // or status; an older worker result can arrive after a newer request.
    if (media.project_generation != 0 &&
        !restoreRevisions_.is_current(media.project_generation)) continue;
    if (media.ok) {
     previousProject_.reset();
     stagedMidiRouting_=false;
     activation_.activated();
     mediaStatus_="Saved Studio One project restored (D1 disk and bank recall)";
    } else {
     if(!rollbackPendingProject())overlayGuard_.restore_warning(true);
     mediaStatus_="Project restore failed: "+media.error;
    }
    continue;
   }
   if(media.slot==eiii::media_slot::wav_import) {
    ++wavSerial_;
    if(activation_.pending())continue;
    if(media.ok) {
     project_.autoload_bank=media.wav_bank;project_.autoload_preset=media.wav_preset;
     autoloadExplicit_=true; // WAV import must not override the user's MIDI routing.
     notifyProjectDirty();
     mediaStatus_="WAV bank D1 B"+std::to_string(media.wav_bank)+" verified; previous bank saved as B"+std::to_string(media.wav_backup);
    } else mediaStatus_="WAV import failed: "+media.error;
    continue;
   }
   if (media.ok) {
    // A queued request is not a committed change. The worker's acceptance
    // is the earliest safe point to notify the DAW; host restores/inspect
    // intentionally do not count as user edits.
    if (media.slot==eiii::media_slot::hd_changes ||
        media.slot==eiii::media_slot::rom ||
        media.slot==eiii::media_slot::converted_cd ||
        media.slot==eiii::media_slot::cem_model ||
        media.slot==eiii::media_slot::scsi ||
        media.slot==eiii::media_slot::floppy)
     notifyProjectDirty();
    if (media.slot==eiii::media_slot::hd_changes)
     mediaStatus_="D"+std::to_string(media.scsi_id)+" disk changes discarded";
    else if (media.slot==eiii::media_slot::rom) mediaStatus_="ROM directory accepted; EIII restarted";
    else if (media.slot==eiii::media_slot::floppy) mediaStatus_=media.path.empty()?"D0 floppy ejected":"D0 floppy inserted (read-only)";
    else if (media.scsi_id==1) mediaStatus_="D1 hard disk accepted; EIII restarted";
    else if (media.scsi_id==3) mediaStatus_=media.path.empty()?"D3 CD ejected":"D3 CD inserted";
    else if (media.slot==eiii::media_slot::scsi && media.scsi_id>=4 && media.scsi_id<=7)
     mediaStatus_="D"+std::to_string(media.scsi_id)+(media.kind==eiii::scsi_kind::none?" removed":" image accepted");
    else mediaStatus_="Media change accepted";
   } else mediaStatus_="Media refused: "+media.error;
  }
  // A rejected overlay must remain visible even if media-result messages arrived.
  const std::string warning=engine_.take_warning();
  if(!warning.empty()) { overlayGuard_.restore_warning(true); mediaStatus_="Media restore warning: "+warning; }
  // A real firmware save may happen without a GUI command (physical front
  // panel writes D1). Only count genuine new writes, never a mount or a
  // published restored overlay. No audio thread messages or disk blob copies.
  const uint64_t firmwareWrites=engine_.hd_firmware_write_serial();
  if(firmwareWrites!=lastFirmwareHdSerial_) {
   lastFirmwareHdSerial_=firmwareWrites;
   notifyProjectDirty();
  }
 }

 eiii::vst3::ProjectState currentProject()
 {
  // Respect an active host restore until the engine is next activated.
  if(activation_.pending() || engine_.wav_import_busy()) return project_;
  pollMedia();
  eiii::vst3::ProjectState state=project_;
  state.midi_channel=uint32_t(midiRouting_.target());
  if(engine_.started()) {
   pollMedia();
   state.rom_dir=engine_.media_path(eiii::media_slot::rom);
   state.hd_path=engine_.media_path(eiii::media_slot::hd);
   state.cd_path=engine_.media_path(eiii::media_slot::cd);
   state.floppy_path=engine_.media_path(eiii::media_slot::floppy);
   state.cem_model=engine_.media_path(eiii::media_slot::cem_model);
   for(unsigned id=4;id<=7;++id) {
    const auto scsi=engine_.scsi(id);
    state.extra_kind[id-4]=uint32_t(scsi.kind);
    state.extra_path[id-4]=scsi.path;
   }
   if(!overlayGuard_.can_replace_overlays()) return project_; // retain the original saved overlay, never replace with empty
   state.hd_changes[0]=engine_.hd_changes(1);
   for(unsigned id=4;id<=7;++id) state.hd_changes[id-3]=engine_.hd_changes(id);
  }
  return state;
 }
 static void capturedMedia(eiii::vst3::ProjectState& state,eiii::runtime_config&& config) {
  state.rom_dir=std::move(config.rom_dir);state.hd_path=std::move(config.hd_path);
  state.cd_path=std::move(config.cd_path);state.floppy_path=std::move(config.floppy_path);
  state.cem_model=std::move(config.cem_model);
  state.hd_changes[0]=std::move(config.hd_changes[1]);
  for(unsigned id=4;id<=7;++id) {
   state.extra_kind[id-4]=uint32_t(config.scsi[id].kind);
   state.extra_path[id-4]=std::move(config.scsi[id].path);
   state.hd_changes[id-3]=std::move(config.hd_changes[id]);
  }
 }
 void configureProject(eiii::runtime_config& cfg)
 {
  if(!projectLoaded_) return;
  namespace fs=std::filesystem;
  auto exists=[](std::string const& s,bool dir) {
   std::error_code error;
   const fs::path p=eiii::shim::path_from_utf8(s);
   return !s.empty() && (dir?fs::is_directory(p,error):fs::is_regular_file(p,error));
  };
  if(exists(project_.rom_dir,true)) cfg.rom_dir=project_.rom_dir;
  else if(!project_.rom_dir.empty()) mediaStatus_="Saved ROM folder not found; using launch setting";
  bool hdMatched=false;
  if(project_.hd_path.empty() && !project_.floppy_path.empty()) cfg.hd_path.clear(); // restore D0-only boot
  if(exists(project_.hd_path,false)) { cfg.hd_path=project_.hd_path; hdMatched=true; }
  else if(!project_.hd_path.empty()) {
   mediaStatus_="Saved D1 disk missing; using launch setting";
   if(!project_.hd_changes[0].empty()) overlayGuard_.missing_disk_with_changes(true);
  }
  if(project_.cd_path.empty() || exists(project_.cd_path,false)) cfg.cd_path=project_.cd_path;
  else mediaStatus_="Saved CD image missing; no disc loaded";
  if(project_.floppy_path.empty() || exists(project_.floppy_path,false)) cfg.floppy_path=project_.floppy_path;
  else {cfg.floppy_path.clear();mediaStatus_="Saved D0 floppy missing; D0 remains ejected";}
  if(project_.cem_model=="v1"||project_.cem_model=="v2") cfg.cem_model=project_.cem_model;
  // Disk deltas are bound to the *specific* image path they were written against.
  if(hdMatched) cfg.hd_changes[1]=project_.hd_changes[0];
  for(unsigned id=4;id<=7;++id) {
   const auto kind=project_.extra_kind[id-4];
   const auto& path=project_.extra_path[id-4];
   if(kind==0) cfg.scsi[id]={};
   else if((kind==2 && path.empty()) || exists(path,false)) {
    cfg.scsi[id].kind=eiii::scsi_kind(kind);
    cfg.scsi[id].path=path;
    if(kind==1) cfg.hd_changes[id]=project_.hd_changes[id-3];
   } else {
    mediaStatus_="Saved additional SCSI image missing; drive ignored";
    if(kind==1 && !project_.hd_changes[id-3].empty()) overlayGuard_.missing_disk_with_changes(true);
   }
  }
 }

 std::vector<float> left_, right_;
 std::vector<float> sampleInputL_, sampleInputR_;
 int32 maxBlock_ = 1024;
 double rate_ = 48000.0;
 bool offlineMode_ = false;
 bool reuseEngine() const {
  // A mode-only host reactivation must not reboot the original sampler or
  // repeat its bank-loading menu sequence. Keep its existing latency too.
  return engine_.started() && engine_.worker_alive() && !activation_.pending() && engine_.sample_rate()==rate_;
 }
 tresult PLUGIN_API setupProcessing(ProcessSetup& setup) override {
  auto r=AudioEffect::setupProcessing(setup);
  if (r!=kResultOk) return r;
  maxBlock_=std::max<int32>(1,setup.maxSamplesPerBlock);
  rate_=setup.sampleRate;
  offlineMode_ = setup.processMode == kOffline;
  setupMode_ = setup.processMode;
  engine_.set_realtime(!offlineMode_);
  left_.resize(maxBlock_); right_.resize(maxBlock_);
  sampleInputL_.resize(maxBlock_); sampleInputR_.resize(maxBlock_);
  return kResultOk;
 }
 tresult PLUGIN_API setActive(TBool state) override {
  if (state) {
   pollMedia();
   const bool pendingWorker=engine_.started()&&engine_.worker_alive()&&
                            activation_.pending()&&engine_.sample_rate()==rate_;
   if(reuseEngine()||pendingWorker) {
    if(!offlineMode_)engine_.ensure_realtime_block(size_t(maxBlock_));
    engine_.set_realtime(!offlineMode_);
    return AudioEffect::setActive(state);
   }
   eiii::engine_config cfg;
   cfg.rt.sample_rate=rate_;
   cfg.realtime=!offlineMode_;
   cfg.max_block=size_t(maxBlock_);
   if (const char* p=std::getenv("EIII_ROM_DIR")) cfg.rt.rom_dir=p;
   if (const char* p=std::getenv("EIII_HD_IMAGE")) cfg.rt.hd_path=p;
   if (const char* p=std::getenv("EIII_CD_IMAGE")) cfg.rt.cd_path=p;
   if (const char* p=std::getenv("EIII_FLOPPY")) cfg.rt.floppy_path=p;
   apply_bundled_firmware(cfg);
   auto readOption=[](const char* name, int minimum, int maximum, int fallback) {
    if (const char* value=std::getenv(name)) {
     char* end=nullptr;
     const long parsed=std::strtol(value,&end,10);
     if (end!=value && *end==0 && parsed>=minimum && parsed<=maximum)
      return int(parsed);
    }
    return fallback;
   };
   cfg.autoload_bank=readOption("EIII_AUTOLOAD_BANK",0,99,-1);
   cfg.autoload_drive=readOption("EIII_AUTOLOAD_DRIVE",1,7,1);
   cfg.autoload_preset=readOption("EIII_AUTOLOAD_PRESET",0,99,-1);
   if(autoloadExplicit_) {
    cfg.autoload_bank=project_.autoload_bank;
    cfg.autoload_drive=1; // D1 proven reliable; other drives remain future work
    cfg.autoload_preset=project_.autoload_preset;
   } else {
    // Legacy PowerShell test configuration also becomes persistent song state.
    project_.autoload_bank=cfg.autoload_bank;
    project_.autoload_preset=cfg.autoload_bank<0?-1:cfg.autoload_preset;
   }
   // Apply host routing staged with a project if the host required a full
   // activation before the worker could accept the late restore.
   if (stagedMidiRouting_) {
     midiRouting_.set_target(stagedMidiTarget_);
     stagedMidiRouting_=false;
   }
   // Keep legacy diagnostic scripts working, but host-saved/user-chosen routing wins.
   if (!savedMidiRouting_)
     midiRouting_.set_target(unsigned(readOption("EIII_TEST_MIDI_CHANNEL",1,16,0)));

   configureProject(cfg.rt);
   // Only a newly created plugin may apply a firmware MIDI default; saved
   // projects retain their own firmware/disk context, never force Omni on restore.
   cfg.first_start_midi_omni = eiii::needs_first_start_midi(
       projectLoaded_, savedMidiRouting_, engineStarts_);
   // Exactly one operator sequence, owned by engine::thread_main().
   // A second operator thread previously issued overlapping scanner events.
   const bool restoringMachine=projectLoaded_ && !project_.machine_snapshot.empty();
   initialMachinePending_=restoringMachine;
   initialMachineGeneration_=projectGeneration_;
   if(restoringMachine)activation_.restored(true);
   // Initial recall is validated before the first CPU instruction/audio frame.
   // There is no cold-boot/autoload machine published ahead of the snapshot.
   engine_.start(cfg,restoringMachine?project_.machine_snapshot:std::vector<uint8_t>{},projectGeneration_);
   ++engineStarts_;
   midiRouting_.reset();
   noteOffRecovery_.reset();
   if(!restoringMachine)activation_.activated();
  } else {
   if(engine_.started()) {
    project_=currentProject();
    projectLoaded_=true;
   }
   // setActive(false) releases the host audio connection, not the sampler's
   // power switch. A VST3 mode change is allowed to deactivate/reactivate us.
   // Keep the machine and queued releases; terminate()/destruction joins it.
   if(engine_.started() && activation_.capture_old_engine()) engine_.refresh_hd_changes();
   pollMedia();
   if(projectLoaded_ && activation_.capture_old_engine() && overlayGuard_.can_replace_overlays()) {
    project_.hd_changes[0]=engine_.hd_changes(1);
    for(unsigned id=4;id<=7;++id)
     project_.hd_changes[id-3]=engine_.hd_changes(id);
   }
  }
  return AudioEffect::setActive(state);
 }
 tresult PLUGIN_API terminate() override {
  #ifdef _WIN32
  headlessDirtyPump_.stop();
#endif
  engine_.shutdown();
  return AudioEffect::terminate();
 }

 tresult PLUGIN_API notify(IMessage* message) override {
  if(!message || !message->getMessageID()) return kInvalidArgument;
  const char* id=message->getMessageID();
  if(std::strcmp(id,"EIII.GUI.DirtyAck")==0) {
   int64 acknowledged=0;
   if(!message->getAttributes() ||
      message->getAttributes()->getInt("revision",acknowledged)!=kResultOk ||
      acknowledged<=0 || uint64_t(acknowledged)>dirtyRevision_.current())
    return kInvalidArgument;
   lastAckedDirty_=std::max(lastAckedDirty_,uint64_t(acknowledged));
   return kResultTrue;
  }
  if(std::strcmp(id,"EIII.GUI.Poll")==0) {
   pollMedia();
   EiiiUiState view{};
   eiii::panel_snapshot snapshot{};
   engine_.snapshot(snapshot);
   view.hardware=snapshot;
   const std::string floppyPath=engine_.started()?engine_.media_path(eiii::media_slot::floppy):project_.floppy_path;
   if(!floppyPath.empty()) {
    const auto p=eiii::shim::path_from_utf8(floppyPath);
    const auto name=eiii::shim::utf8_from_path(p.filename());
    std::snprintf(view.floppy_name,sizeof(view.floppy_name),"%s",name.c_str());
   }
   std::snprintf(view.media_status,sizeof(view.media_status),"%s",mediaStatus_.c_str());
   view.running=snapshot.running?1u:0u;
   view.wav_busy=engine_.wav_import_busy()?1u:0u;view.wav_serial=wavSerial_;
   view.project_dirty_revision=dirtyRevision_.current();
   view.led_bits=(~uint32_t(snapshot.led_latch)) & 0xffffu;
   view.seconds=uint32_t(snapshot.emu_seconds);
   view.setup_mode=setupMode_;view.process_mode=lastProcessMode_.load();
   view.engine_starts=engineStarts_;view.latency_frames=getLatencySamples();
   const auto renderStats=engine_.stats();
   view.rendered_frames=renderStats.frames_out;
   view.underrun_frames=renderStats.underrun_frames;
   view.midi_dropped=renderStats.midi_dropped;
   view.midi_events=renderStats.midi_events;view.midi_late_frames=renderStats.midi_late_frames;
   view.midi_trace_hash=renderStats.midi_trace_hash;
   view.realtime_frames=processFrames_[kRealtime].load();
   view.offline_frames=processFrames_[kOffline].load();
   view.prefetch_frames=processFrames_[kPrefetch].load();
   for(int row=0;row<4;++row) {
    const int base=row==0?0:row==1?0x40:row==2?0x14:0x54;
    for(int i=0;i<20;++i) {
     const unsigned char c=snapshot.lcd.ddram[base+i];
     view.lcd[row][i]=(c>=32 && c<=126)?char(c):' ';
    }
   }
   std::memcpy(view.status,snapshot.status,
       std::min(sizeof(view.status)-1,sizeof(snapshot.status)));
   view.midi_note_ons=midiNoteOns_.load(std::memory_order_relaxed);
   view.last_midi_channel=lastMidiChannel_.load(std::memory_order_relaxed);
   view.forwarded_midi_channel=uint32_t(midiRouting_.target());
   view.autoload_bank=project_.autoload_bank;
   view.autoload_preset=project_.autoload_preset;
   view.output_peak_milli=outputPeakMilli_.load(std::memory_order_relaxed);
   // GUI polling must not copy potentially hundreds of MB of HD deltas.
   // Only project saves need a full serialized media snapshot.
   const std::string analogModel=activation_.pending()
     ? project_.cem_model
     : (engine_.started() ? engine_.media_path(eiii::media_slot::cem_model) : project_.cem_model);
   view.analog_model=analogModel=="v2"?2u:1u;
   IMessage* response=allocateMessage();
   if(!response) return kResultFalse;
   response->setMessageID("EIII.GUI.Status");
   response->getAttributes()->setBinary("payload",&view,uint32(sizeof(view)));
   const tresult result=sendMessage(response);
   response->release();
   return result;
  }
  if(std::strcmp(id,"EIII.GUI.WavCancel")==0) {engine_.cancel_wav_import();return kResultTrue;}
  if(std::strcmp(id,"EIII.GUI.WavImport")==0) {
   auto* a=message->getAttributes();int64 root=-1,low=-1,high=-1,preset=-1;
   const void* bytes=nullptr;uint32 len=0;
   if(!a || a->getInt("root",root)!=kResultOk || a->getInt("low",low)!=kResultOk ||
      a->getInt("high",high)!=kResultOk || a->getInt("preset",preset)!=kResultOk ||
      root<0 || root>127 || low<0 || low>root || high<root || high>127 || preset<0 || preset>99 ||
      a->getBinary("path",bytes,len)!=kResultOk || !bytes || !len || len>4096 || std::memchr(bytes,0,len))return kInvalidArgument;
   if(engine_.wav_import_busy() || activation_.pending())return kResultFalse;
   if(!engine_.started() || !engine_.refresh_hd_changes()) {mediaStatus_="Start EIII and wait for media before WAV import";return kResultFalse;}
   project_=currentProject();projectLoaded_=true; // committed save snapshot during import
   const bool configured=!engine_.media_path(eiii::media_slot::rom).empty() &&
                         !engine_.media_path(eiii::media_slot::hd).empty();
   if(configured) {
    eiii::runtime_config config;
    if(!engine_.capture_project(project_.machine_snapshot,config))return kResultFalse;
    capturedMedia(project_,std::move(config));
   }
   const std::string path(static_cast<const char*>(bytes),len);
   if(!engine_.import_wav(path,{unsigned(root),unsigned(low),unsigned(high),unsigned(preset)})) {
    mediaStatus_="WAV import could not be queued";return kResultFalse;
   }
   mediaStatus_="WAV import: checking, recording and saving; MEDIA > Cancel WAV import to abort";
   return kResultTrue;
  }
  if(engine_.wav_import_busy())return kResultFalse;
  if(std::strcmp(id,"EIII.GUI.Control")==0) {
   int64 index=-1, down=0;
   auto* a=message->getAttributes();
   if(!a || a->getInt("index",index)!=kResultOk ||
      a->getInt("down",down)!=kResultOk ||
      index<0 || size_t(index)>=eiii::panel_control_count ||
      (down!=0 && down!=1)) return kInvalidArgument;
   const auto& k=eiii::panel_controls[size_t(index)];
   if(k.kind!=eiii::panel_kind::button) return kInvalidArgument;
   if(k.route==eiii::panel_route::scan)
    return engine_.post({eiii::panel_input::SCAN,k.code,uint8_t(down),0})?kResultTrue:kResultFalse;
   if(k.route==eiii::panel_route::xsw)
    return engine_.post({eiii::panel_input::XSW,k.code,uint8_t(down),0})?kResultTrue:kResultFalse;
   if(k.route==eiii::panel_route::host && !std::strcmp(k.id,"power") && down==1) {
    engine_.power(!engine_.powered()); return kResultTrue;
   }
   return kResultFalse;
  }
  if(std::strcmp(id,"EIII.GUI.Media")==0) {
   int64 action=-1;
   auto* attrs=message->getAttributes();
   if(!attrs || attrs->getInt("action",action)!=kResultOk ||
      action<1 || action>7) return kInvalidArgument;
   std::string mediaPath;
   if(action!=4 && action!=7) {
    const void* bytes=nullptr;
    uint32 len=0;
    if(attrs->getBinary("path",bytes,len)!=kResultOk || !bytes ||
       len==0 || len>4096 || std::memchr(bytes,0,len))
      return kInvalidArgument;
    mediaPath.assign(static_cast<const char*>(bytes),size_t(len));
   }
   const auto slot=action==6||action==7?eiii::media_slot::floppy:
                   action==1?eiii::media_slot::rom:
                   action==2?eiii::media_slot::hd:
                   action==5?eiii::media_slot::converted_cd:eiii::media_slot::cd;
   // Original runtime checks all media formats and keeps the previous image on error.
   if(!engine_.set_media(slot,mediaPath)) {
    mediaStatus_="Media queue busy; try again";
    return kResultFalse;
   }
   mediaStatus_="Checking selected media...";
   return kResultTrue;
  }
  if(std::strcmp(id,"EIII.GUI.Scsi")==0) {
   int64 drive=-1,kind=-1;
   auto* a=message->getAttributes();
   if(!a || a->getInt("drive",drive)!=kResultOk ||
      a->getInt("kind",kind)!=kResultOk || drive<4 || drive>7 || kind<0 || kind>2)
    return kInvalidArgument;
   std::string path;
   if(kind!=0) {
    const void* bytes=nullptr;
    uint32 len=0;
    if(a->getBinary("path",bytes,len)!=kResultOk || !bytes || !len ||
       len>4096 || std::memchr(bytes,0,len)) return kInvalidArgument;
    path.assign(static_cast<const char*>(bytes),size_t(len));
   }
   if(!engine_.set_scsi(unsigned(drive),eiii::scsi_kind(kind),path)) {
    mediaStatus_="Extra drive queue busy; try again";
    return kResultFalse;
   }
   mediaStatus_="Checking extra SCSI image...";
   return kResultTrue;
  }
  if(std::strcmp(id,"EIII.GUI.ExportDisk")==0) {
   int64 disk=0;
   auto* a=message->getAttributes();
   const void* bytes=nullptr;
   uint32 len=0;
   if(!a || a->getInt("drive",disk)!=kResultOk ||
      (disk!=1 && (disk<4 || disk>7)) ||
      a->getBinary("path",bytes,len)!=kResultOk || !bytes ||
      len==0 || len>4096 || std::memchr(bytes,0,len)) return kInvalidArgument;
   if(exportThread_.joinable()) {
    if(!exportFinished_.load(std::memory_order_acquire)) {
     mediaStatus_="Disk export already running";
     return kResultFalse;
    }
    pollMedia();
   }
   const auto drive=unsigned(disk);
   const auto diskInfo=engine_.scsi(drive);
   if(diskInfo.kind!=eiii::scsi_kind::hd || diskInfo.path.empty()) {
    mediaStatus_="Selected drive has no hard disk image";
    return kResultFalse;
   }
   std::string changes;
   // Disk writes become visible in the public cache after an idle interval;
   // force a worker-owned flush rather than exporting that stale cache.
   // Fail closed on timeout, as with VST3 getState().
   if(!eiii::vst3::capture_disk_export_overlay(
        [this] { return !engine_.started() || engine_.refresh_hd_changes(); },
        [this,drive] { return engine_.hd_changes(drive); },changes)) {
    mediaStatus_="Unable to capture current disk changes; retry export";
    return kResultFalse;
   }
   if(changes.empty()) {
    mediaStatus_="No virtual disk changes to export";
    return kResultFalse;
   }
   const std::string output(static_cast<const char*>(bytes),size_t(len));
   namespace fs=std::filesystem;
   std::error_code ec;
   const fs::path destination=eiii::shim::path_from_utf8(output);
   const fs::path source=eiii::shim::path_from_utf8(diskInfo.path);
   if(destination.empty()||source.empty()){mediaStatus_="Invalid UTF-8 disk path";return kInvalidArgument;}
   if(fs::exists(destination,ec) || ec ||
      fs::absolute(destination,ec).lexically_normal()==fs::absolute(source,ec).lexically_normal() || ec) {
    mediaStatus_="Choose a NEW output file; existing files cannot be overwritten";
    return kResultFalse;
   }
   const std::string image=diskInfo.path;
   exportFinished_.store(false,std::memory_order_relaxed);
   mediaStatus_="Exporting D"+std::to_string(drive)+" image...";
   try {
    exportThread_=std::thread([this,image,changes,output] {
     eiii::vst3::finish_disk_export([&]() -> std::string {
      std::string error;
      const bool ok=eiii::runtime::write_image_with_changes(image,changes,output,error);
      return ok?"Disk image exported: "+output:"Disk export failed: "+error;
     },exportResult_,exportFinished_);
    });
   } catch(...) {
    mediaStatus_="Unable to start disk export";
    return kResultFalse;
   }
   return kResultTrue;
  }
  if(std::strcmp(id,"EIII.GUI.DiscardDiskChanges")==0) {
   int64 disk=0;
   auto* a=message->getAttributes();
   if(!a || a->getInt("drive",disk)!=kResultOk ||
      (disk!=1 && (disk<4 || disk>7))) return kInvalidArgument;
   const auto drive=unsigned(disk);
   const auto media=engine_.scsi(drive);
   if(media.kind!=eiii::scsi_kind::hd) {
    mediaStatus_="D"+std::to_string(drive)+" is not a hard disk";
    return kResultFalse;
   }
   // As with export, the public HD cache may lag recent firmware writes
   // by the normal coalescing interval. Never pronounce an active disk clean
   // or queue a discard based on that stale cache.
   if(engine_.started() && !engine_.refresh_hd_changes()) {
    mediaStatus_="Unable to verify current disk changes; retry discard";
    return kResultFalse;
   }
   if(engine_.hd_changes(drive).empty()) {
    mediaStatus_="D"+std::to_string(drive)+" has no saved disk changes";
    return kResultTrue;
   }
   if(!engine_.set_hd_changes(drive,"")) {
    mediaStatus_="Disk changes queue busy; try again";
    return kResultFalse;
   }
   mediaStatus_="Discarding D"+std::to_string(drive)+" changes...";
   return kResultTrue;
  }
  if(std::strcmp(id,"EIII.GUI.AnalogModel")==0) {
   int64 model=0;
   auto* a=message->getAttributes();
   if(!a || a->getInt("model",model)!=kResultOk || (model!=1 && model!=2))
    return kInvalidArgument;
   const char* value=model==1?"v1":"v2";
   if(!engine_.set_media(eiii::media_slot::cem_model,value)) {
    mediaStatus_="Analog model queue busy; try again";
    return kResultFalse;
   }
   mediaStatus_="Updating analog model...";
   return kResultTrue;
  }
  if(std::strcmp(id,"EIII.GUI.AutoRecall")==0) {
   int64 bank=-2,preset=-2;
   auto* a=message->getAttributes();
   if(!a || a->getInt("bank",bank)!=kResultOk ||
      a->getInt("preset",preset)!=kResultOk ||
      bank< -1 || bank>99 || preset< -1 || preset>99 ||
      (bank<0 && preset>=0)) return kInvalidArgument;
   const bool changed = project_.autoload_bank != int32_t(bank) ||
                        project_.autoload_preset != int32_t(preset);
   project_.autoload_bank=int32_t(bank);
   project_.autoload_preset=int32_t(preset);
   autoloadExplicit_=true;
   if(changed) notifyProjectDirty();
   // The real hardware macro runs only at power-on. No unrequested reset.
   mediaStatus_="D1 recall saved; takes effect on next plugin activation";
   return kResultTrue;
  }
  if(std::strcmp(id,"EIII.GUI.MidiRouting")==0) {
   int64 requested=-1;
   auto* a=message->getAttributes();
   if (!a || a->getInt("target",requested)!=kResultOk ||
       requested<0 || requested>16) return kInvalidArgument;
   const bool changed=midiRouting_.target()!=unsigned(requested);
   restoreRevisions_.manual_route_chosen([this,requested] {
    midiRouting_.set_target(unsigned(requested));
   });
   if(changed) notifyProjectDirty();
   stagedMidiRouting_=false; // explicit user input wins against a queued project route
   savedMidiRouting_=true;
   return kResultTrue;
  }
  if(std::strcmp(id,"EIII.GUI.Slider")==0) {
   int64 index=-1, value=0;
   auto* a=message->getAttributes();
   if(!a || a->getInt("index",index)!=kResultOk ||
      a->getInt("adc",value)!=kResultOk ||
      index<0 || size_t(index)>=eiii::panel_control_count ||
      value<0 || value>0x7fff) return kInvalidArgument;
   const auto& k=eiii::panel_controls[size_t(index)];
   if(k.kind!=eiii::panel_kind::slider || k.route!=eiii::panel_route::adc)
    return kInvalidArgument;
   return engine_.post({eiii::panel_input::ADC,k.code,0,uint16_t(value)})?kResultTrue:kResultFalse;
  }
  return AudioEffect::notify(message);
 }
 // Version 3: v2 media/disk overlays plus firmware-driven D1 bank/preset recall.
 // In contrast to the physical sampler, original HD images are always read-only.
 tresult PLUGIN_API getState(IBStream* stream) override {
  try {
  if(!stream) return kInvalidArgument;
  // Never silently serialize stale disk writes during live host saves.
  if(!engine_.wav_import_busy() && engine_.started() && activation_.capture_old_engine() && !engine_.refresh_hd_changes())
   return kResultFalse;
  pollMedia();
  // Never combine pending foreign metadata with the outgoing live machine.
  if(activation_.pending())return kResultFalse;
  if(!overlayGuard_.can_save()) return kResultFalse; // refuse destructive host save
  auto snapshot=engine_.wav_import_busy()?project_:currentProject();
  // A D0 project cannot be saved from a stale pre-import snapshot.
  // Its full WD1772/head/PLL image must be captured from the active worker.
  const bool poweredD0=engine_.started() && engine_.powered() &&
     (!snapshot.floppy_path.empty() || !engine_.media_path(eiii::media_slot::floppy).empty());
  if(poweredD0 && engine_.wav_import_busy()) {
   mediaStatus_="Finish or cancel WAV import before saving a D0 project";
   return kResultFalse;
  }
  // A host can save an unconfigured instance without ROM or OS media.
  // No emulated machine exists in that case; preserve the media-only state.
  // Once both media paths are configured, a failed live capture must fail closed.
  const bool machineConfigured = !engine_.media_path(eiii::media_slot::rom).empty() &&
      (!engine_.media_path(eiii::media_slot::hd).empty() ||
       !engine_.media_path(eiii::media_slot::floppy).empty());
  if(engine_.started() && engine_.powered() && machineConfigured && !engine_.wav_import_busy()) {
   std::vector<uint8_t> machine;
   eiii::runtime_config config;
   if(!engine_.capture_project(machine,config)) return kResultFalse;
   capturedMedia(snapshot,std::move(config));
   snapshot.machine_snapshot=std::move(machine);
  } else if(engine_.started() && !engine_.powered() && !engine_.wav_import_busy()) {
   // POWER OFF destroyed the volatile machine. Do not pair an older imported
   // checkpoint with today's media/overlays; preserve the legacy cold-boot state.
   snapshot.machine_snapshot.clear();
  }
  // A powered D0 must NEVER produce a media-path-only success. Decode
  // the newly captured full snapshot and require its final section 60.
  if(poweredD0) {
   if(snapshot.floppy_path.empty() || snapshot.machine_snapshot.empty())return kResultFalse;
   std::vector<eiii::vst3::CoreSectionContainer::Section> sections;
   if(!eiii::vst3::CoreSectionContainer::decode(
      snapshot.machine_snapshot.data(),snapshot.machine_snapshot.size(),sections) ||
      sections.empty() || sections.back().id!=60)return kResultFalse;
   std::vector<uint8_t> payload;
   if(!eiii::vst3::CoreStateEnvelope::decode(
       sections.back().bytes.data(),sections.back().bytes.size(),payload) ||
      payload.size()<12 ||
      eiii::vst3::CoreStateEnvelope::get32(payload.data())!=eiii::vst3::CoreD0Format::tag ||
      eiii::vst3::CoreStateEnvelope::get32(payload.data()+4)!=eiii::vst3::CoreD0Format::version ||
      !(eiii::vst3::CoreStateEnvelope::get32(payload.data()+8)&1u))return kResultFalse;
  }
  std::vector<uint8_t> bytes;
  if(!eiii::vst3::encode_state(snapshot,bytes)) return kResultFalse;
  size_t offset=0;
  while(offset<bytes.size()) {
   const int32 requested=int32(std::min<size_t>(bytes.size()-offset,65536u));
   int32 written=0;
   const tresult status=stream->write(bytes.data()+offset,requested,&written);
   if(status!=kResultOk || written<=0 || written>requested) return kResultFalse;
   offset+=size_t(written);
  }
  return kResultOk;
  } catch(...) {
   // VST3 calls must not propagate allocation/stream exceptions into the host.
   return kResultFalse;
  }
 }
 tresult PLUGIN_API setState(IBStream* stream) override {
  bool transactionStaged=false;
  const auto oldGuard=overlayGuard_;
  std::string oldStatus;
  try {
  oldStatus=mediaStatus_;
  if(!stream) return kInvalidArgument;
  if(engine_.wav_import_busy()) {
   mediaStatus_="Finish or cancel WAV import before restoring a project";return kResultFalse;
  }
  std::vector<uint8_t> bytes;
  std::array<uint8_t,65536> chunk{};
  for(;;) {
   int32 count=0;
   const tresult result=stream->read(chunk.data(),int32(chunk.size()),&count);
   if(count<0 || count>int32(chunk.size()) ||
      bytes.size()+size_t(count)>eiii::vst3::kMaxStateBytes) return kResultFalse;
   if(count>0) bytes.insert(bytes.end(),chunk.begin(),chunk.begin()+count);
   if(count==0) break;
   if(result!=kResultOk) break; // Some Steinberg streams return false with a final short block.
  }
  // Legacy plugin versions had no state. Do not overwrite existing media settings.
  if(bytes.empty()) return kResultOk;
  eiii::vst3::DecodedState decoded;
  if(!eiii::vst3::decode_state(bytes.data(),bytes.size(),decoded)) return kResultFalse;
  if(!decoded.value.machine_snapshot.empty()) {
   // Validate framing/topology/checksums before staging
   // routing or replacing any project metadata. Device semantics are checked
   // again by the independently constructed worker candidate.
   std::vector<eiii::vst3::CoreSectionContainer::Section> sections;
   const auto& snapshot=decoded.value.machine_snapshot;
   std::array<bool,8> disks{},cds{};disks[1]=true;cds[3]=true;
   for(unsigned id=4;id<=7;++id) {
    disks[id]=decoded.value.extra_kind[id-4]==1;
    cds[id]=decoded.value.extra_kind[id-4]==2;
   }
   if(!eiii::vst3::CoreSectionContainer::decode(snapshot.data(),snapshot.size(),sections) ||
      !eiii::vst3::CoreSectionRoster::valid(sections,disks,cds,!decoded.value.floppy_path.empty()))return kResultFalse;
   for(const auto& section:sections) {
    std::vector<uint8_t> payload;
    if(!eiii::vst3::CoreStateEnvelope::decode(section.bytes.data(),section.bytes.size(),payload))return kResultFalse;
   }
  }
  if(!decoded.midi_only_v1) {
   // Old releases stored Windows dialog paths in CP_ACP; current runtime
   // expects strict UTF-8. Convert ONLY invalid UTF-8 by a lossless ANSI
   // roundtrip, before touching the live engine or MIDI routing.
   auto migrate=[](std::string& path){return eiii::shim::upgrade_legacy_ansi_path(path);};
   if(!migrate(decoded.value.rom_dir) || !migrate(decoded.value.hd_path) ||
      !migrate(decoded.value.cd_path) || !migrate(decoded.value.floppy_path)) return kResultFalse;
   for(auto& path:decoded.value.extra_path)
    if(!migrate(path))return kResultFalse;
   if(!eiii::vst3::valid_state(decoded.value))return kResultFalse;
  }
  if(decoded.midi_only_v1 && initialMachinePending_ && !previousProject_) {
   // MIDI-only recall preserves the already selected initial machine. It has
   // no replacement media/RAM; canceling that first boot would strand a new
   // instance pending forever. Treat its route like a deliberate user edit.
   restoreRevisions_.manual_route_chosen([this,&decoded]() noexcept {
    midiRouting_.set_target(decoded.value.midi_channel);
   });
   savedMidiRouting_=true;return kResultOk;
  }
  // Reject a busy/saturated transaction before changing generations or metadata.
  // WAV imports are refused above and retain their own rollback checkpoint.
  if(!decoded.midi_only_v1 && engine_.started() && !engine_.can_queue_project_restore())return kResultFalse;
  const bool predecessorPending=previousProject_.has_value() && activation_.pending();
  const uint64_t predecessorGeneration=projectGeneration_;
  if(!decoded.midi_only_v1 && engine_.started() && !previousProject_) {
   previousProject_=currentProject();previousAutoloadExplicit_=autoloadExplicit_;
   previousProjectLoaded_=projectLoaded_;previousSavedMidiRouting_=savedMidiRouting_;
   previousRouteRevision_=restoreRevisions_.route_revision();
  }
  const unsigned restoredMidiTarget=decoded.value.midi_channel;
  // Invalidate all older worker completions, including when a legacy
  // MIDI-only state supersedes a full project restore.
  const uint64_t projectGeneration=restoreRevisions_.new_project();
  transactionStaged=true;
  projectGeneration_=projectGeneration;
  const uint64_t committed=engine_.invalidate_project_restores(projectGeneration);
  if(predecessorPending && committed==predecessorGeneration) {
   // It committed before invalidation, even if no editor/UI poll collected
   // its result. On rejection retain that accepted project, not its ancestor.
   previousProject_=std::move(project_);
   previousAutoloadExplicit_=autoloadExplicit_;previousProjectLoaded_=projectLoaded_;
   previousSavedMidiRouting_=savedMidiRouting_;
   previousRouteRevision_=restoreRevisions_.route_revision();
  }
  if(decoded.midi_only_v1)rollbackPendingProject();
  const uint64_t routeRevision=restoreRevisions_.route_revision();
  // Never change live HOST routing before its candidate firmware/D1 state
  // has been accepted: a failed overlay must leave *both* audio and routing.
  if(decoded.midi_only_v1)
    restoreRevisions_.manual_route_chosen([this,restoredMidiTarget]() noexcept {midiRouting_.set_target(restoredMidiTarget);});
  else if(!engine_.started())midiRouting_.set_target(restoredMidiTarget);
  else {
    stagedMidiTarget_=restoredMidiTarget;
    stagedMidiRouting_=true;
  }
  savedMidiRouting_=true;
  if(!decoded.midi_only_v1) {
   overlayGuard_.new_project();
   project_=std::move(decoded.value);
   projectLoaded_=true;
   autoloadExplicit_=decoded.has_autoload_v3;
   // Never call shutdown/start while the host's audio thread might process().
   // Loaded live media is staged for the next plugin activation.
   activation_.restored(engine_.started());
   if (activation_.pending()) {
    // Some hosts call setState() after activating the plugin. Previously the
    // saved disk/firmware context remained staged indefinitely unless the
    // host happened to deactivate/reactivate. Queue a complete worker-owned
    // transaction instead; never shutdown/start from the host audio callback.
    eiii::engine_config cfg;
    cfg.rt.sample_rate=engine_.sample_rate();
    cfg.realtime=!offlineMode_;
    cfg.max_block=size_t(maxBlock_);
    cfg.rt.rom_dir=engine_.media_path(eiii::media_slot::rom);
    cfg.rt.hd_path=engine_.media_path(eiii::media_slot::hd);
    cfg.rt.cd_path=engine_.media_path(eiii::media_slot::cd);
    cfg.rt.floppy_path=engine_.media_path(eiii::media_slot::floppy);
    cfg.rt.cem_model=engine_.media_path(eiii::media_slot::cem_model);
    for(unsigned id=4;id<=7;++id) cfg.rt.scsi[id]=engine_.scsi(id);
    cfg.autoload_bank=project_.autoload_bank;
    cfg.autoload_preset=project_.autoload_preset;
    cfg.autoload_drive=1;
    cfg.first_start_midi_omni=false;
    configureProject(cfg.rt);
    // Allocate diagnostic text before publishing the request. Nothing may
    // throw after successful queue publication and falsely report rejection.
    mediaStatus_="Restoring saved Studio One firmware/disk state...";
    if (overlayGuard_.can_save() && engine_.restore_project(cfg,
         [this,restoredMidiTarget,routeRevision]() noexcept {
          // Callback executes on the emulator worker, so only atomic
          // revisions (not GUI booleans) may be read here.
          restoreRevisions_.commit_owner_route(routeRevision,
           [this,restoredMidiTarget] { midiRouting_.set_target(restoredMidiTarget); });
         },projectGeneration,project_.machine_snapshot)) {
     // Commit is asynchronous; status/metadata are already staged.
    } else {
     rollbackPendingProject();mediaStatus_="Saved project restore could not be queued";
     return kResultFalse;
    }
   }
  }
  return kResultOk;
  } catch(...) {
   if(transactionStaged) {
    rollbackPendingProject();
    overlayGuard_=oldGuard;
    mediaStatus_.swap(oldStatus);
   }
   return kResultFalse;
  }
 }
 static FUnknown* create(void*) { return (IAudioProcessor*)new Processor; }
 tresult PLUGIN_API initialize(FUnknown* ctx) override {
  auto r=AudioEffect::initialize(ctx);
  if(r!=kResultOk) return r;
  addAudioOutput(STR16("Main Out"), SpeakerArr::kStereo);
  // EIII ADC input is a separate auxiliary stereo bus (sidechain routing).
  // Existing instruments without a connected send continue playing normally.
  addAudioInput(STR16("Sample In"), SpeakerArr::kStereo, BusTypes::kAux);
  addEventInput(STR16("MIDI In"), 1);
  // VST3 initialize runs in the host UI thread. The Windows message loop
  // delivers this timer regardless of whether an editor view was created.
  // If the host does not pump Win32 messages, explicit host callbacks still
  // retain their previous behavior; never schedule host calls on audio.
#ifdef _WIN32
  headlessDirtyPump_.start(this,[](void* opaque) {
   static_cast<Processor*>(opaque)->headlessDirtyTick();
  });
#endif
  return kResultOk;
 }
 // Explicit bus contract: stereo output and stereo auxiliary ADC input.
 // Reject unsupported layouts rather than silently misrouting channels.
 tresult PLUGIN_API setBusArrangements(SpeakerArrangement* inputs, int32 numIns,
                                      SpeakerArrangement* outputs, int32 numOuts) override {
  if(numOuts!=1 || !outputs || outputs[0]!=SpeakerArr::kStereo) return kResultFalse;
  if(numIns!=1 || !inputs || inputs[0]!=SpeakerArr::kStereo) return kResultFalse;
  return AudioEffect::setBusArrangements(inputs,numIns,outputs,numOuts);
 }
 uint32 PLUGIN_API getLatencySamples() override {
  const auto required=eiii::latency_for(rate_,size_t(maxBlock_),1u<<15);
  if(!reuseEngine())return required;
  return offlineMode_?engine_.latency():std::max(required,engine_.latency());
 }
 tresult PLUGIN_API setProcessing(TBool) override { return kResultOk; }
 tresult PLUGIN_API process(ProcessData& d) override {
  if (d.numSamples<0 || d.numSamples>maxBlock_) return kResultFalse;
  // ProcessData is authoritative for this callback. Prefetch stays nonblocking;
  // only an actual kOffline call may wait for emulator output.
  engine_.set_realtime(d.processMode!=kOffline);
  lastProcessMode_.store(d.processMode,std::memory_order_relaxed);
  if(d.processMode>=kRealtime && d.processMode<=kOffline)
   processFrames_[size_t(d.processMode)].fetch_add(uint64_t(d.numSamples),std::memory_order_relaxed);
  if(engine_.wav_import_busy()) {
   for(int32 b=0;b<d.numOutputs;++b) {
    auto& out=d.outputs[b];
    for(int32 c=0;c<out.numChannels;++c) {
     if(d.symbolicSampleSize==kSample32 && out.channelBuffers32 && out.channelBuffers32[c])std::fill_n(out.channelBuffers32[c],d.numSamples,0.f);
     else if(d.symbolicSampleSize==kSample64 && out.channelBuffers64 && out.channelBuffers64[c])std::fill_n(out.channelBuffers64[c],d.numSamples,0.);
    }
    out.silenceFlags=out.numChannels>=64?~uint64(0):((uint64(1)<<out.numChannels)-1);
   }
   outputPeakMilli_.store(0);return kResultOk;
  }
  // Retry previously rejected releases first; never allocate or wait on audio thread.
  if (engine_.started()) retryNoteOffs();
  // Collect MIDI events and VST3 controller points into a single timeline.
  // Fixed capacity: never allocate on the realtime audio thread.
  constexpr size_t kMaxMidiPoints=8192;
  size_t npoints=0;
  uint32 order=0;
  bool overflow=false;
  if(engine_.started() && d.inputEvents)
   for(int32 i=0;i<d.inputEvents->getEventCount();++i) {
    Event ev{};
    if(d.inputEvents->getEvent(i,ev)!=kResultOk) continue;
    if(npoints==kMaxMidiPoints) { overflow=true;break; }
    auto &point=midiTimeline_[npoints++];
    point.offset=std::clamp(ev.sampleOffset,0,d.numSamples);
    point.order=order++;
    point.controller=false;
    point.event=ev;
   }
  if(engine_.started() && d.inputParameterChanges)
   for(int32 q=0;q<d.inputParameterChanges->getParameterCount();++q) {
    auto* queue=d.inputParameterChanges->getParameterData(q);
    if(!queue) continue;
    const uint32 id=queue->getParameterId();
    if(id<eiii::vst3::MidiControllers::base ||
       id>=eiii::vst3::MidiControllers::base+eiii::vst3::MidiControllers::total) continue;
    for(int32 k=0;k<queue->getPointCount();++k) {
     int32 offset=0; ParamValue value=0;
     if(queue->getPoint(k,offset,value)!=kResultOk) continue;
     if(npoints==kMaxMidiPoints) { overflow=true;break; }
     auto &point=midiTimeline_[npoints++];
     point.offset=std::clamp(offset,0,d.numSamples);
     point.order=order++;
     point.controller=true;
     point.id=id;
     point.value=value;
    }
   }
  if(overflow) return kResultFalse; // explicit failure, never silently lose releases
  std::sort(midiTimeline_.begin(),midiTimeline_.begin()+npoints,
            eiii::vst3::MidiTimeOrder{});
  for(size_t t=0;t<npoints;++t) {
   const auto &point=midiTimeline_[t];
   if(point.controller) {
    uint8_t msg[3]{};
    if(eiii::vst3::MidiControllers::decode(point.id,point.value,msg))
     eiii::vst3::forward_controller(midiRouting_,noteOffRecovery_,msg,
       [this,&point](const uint8_t* bytes,unsigned len) noexcept {
        return engine_.midi(uint32_t(point.offset),bytes,uint8_t(len));
       });
    continue;
   }
   const Event& ev=point.event;
   uint8_t msg[3]{};
    if (ev.type==Event::kNoteOnEvent) {
     msg[1]=uint8_t(ev.noteOn.pitch & 127);
     msg[2]=uint8_t(std::clamp(ev.noteOn.velocity,0.f,1.f)*127.f);
     const uint8_t sourceChannel=uint8_t(ev.noteOn.channel & 15);
     if (msg[2]==0) {
      // Velocity-zero NoteOn is a NoteOff. Queue it for retry on saturation.
      msg[0]=uint8_t(0x80 | midiRouting_.peek_note_off(sourceChannel,msg[1]));
      if (noteOffRecovery_.deliver(msg,[&](const uint8_t* bytes) noexcept {
          return engine_.midi(uint32_t(std::clamp(ev.sampleOffset,0,d.numSamples)),bytes,3);
         }))
       midiRouting_.note_off(sourceChannel,msg[1]);
      continue;
     }
     const uint8_t output=midiRouting_.note_on(sourceChannel,msg[1]);
     if (output == eiii::vst3::MidiRouting::kRejected) continue;
     msg[0]=uint8_t(0x90 | output);
     if (!engine_.midi(uint32_t(std::clamp(ev.sampleOffset,0,d.numSamples)),msg,3)) {
      midiRouting_.cancel_note_on(sourceChannel,msg[1]);
      continue;
     }
     if (msg[2]!=0) {
      midiNoteOns_.fetch_add(1,std::memory_order_relaxed);
      lastMidiChannel_.store(uint32_t(ev.noteOn.channel & 15)+1,std::memory_order_relaxed);
     }
    } else if (ev.type==Event::kNoteOffEvent) {
     msg[1]=uint8_t(ev.noteOff.pitch & 127);
     const uint8_t sourceChannel = uint8_t(ev.noteOff.channel & 15);
     msg[0]=uint8_t(0x80 | midiRouting_.peek_note_off(sourceChannel,msg[1]));
     msg[2]=uint8_t(std::clamp(ev.noteOff.velocity,0.f,1.f)*127.f);
     if (noteOffRecovery_.deliver(msg,[&](const uint8_t* bytes) noexcept {
          return engine_.midi(uint32_t(std::clamp(ev.sampleOffset,0,d.numSamples)),bytes,3);
         }))
      midiRouting_.note_off(sourceChannel,msg[1]);
    } else if (ev.type==Event::kPolyPressureEvent) {
     msg[0]=uint8_t(0xa0 | (ev.polyPressure.channel & 15));
     msg[1]=uint8_t(ev.polyPressure.pitch & 127);
     msg[2]=uint8_t(std::clamp(ev.polyPressure.pressure,0.f,1.f)*127.f);
     eiii::vst3::forward_poly_pressure(midiRouting_,msg,
       [this,&point](const uint8_t* bytes,unsigned len) noexcept {
        return engine_.midi(uint32_t(point.offset),bytes,uint8_t(len));
       });
    }
  }
  // Sample In -> original EIII ADC board, exactly as in the upstream CLAP
  // implementation: feed each block BEFORE pull(), on the same frame clock.
  // Read VST3 input buffers without allocation on the realtime audio thread.
  if(engine_.started()) {
   eiii::vst3::SampleInput sample{};
   if(d.numInputs>0 && d.inputs) {
    const auto& input=d.inputs[0];
    sample.channels=input.numChannels>0
        ? unsigned(std::min<int32>(input.numChannels,2)):0;
    sample.is64=d.symbolicSampleSize==kSample64;
    sample.silence_flags=uint64_t(input.silenceFlags);
    if(d.symbolicSampleSize==kSample32)
     sample.data32=const_cast<const float* const*>(input.channelBuffers32);
    else if(d.symbolicSampleSize==kSample64)
     sample.data64=const_cast<const double* const*>(input.channelBuffers64);
    else sample.channels=0;
   }
   eiii::vst3::copy_sample_input(sample,sampleInputL_.data(),sampleInputR_.data(),
                                   size_t(d.numSamples));
   engine_.input(sampleInputL_.data(),sampleInputR_.data(),size_t(d.numSamples));
   engine_.pull(left_.data(),right_.data(),size_t(d.numSamples));
  }
  else { std::fill_n(left_.data(),d.numSamples,0.f); std::fill_n(right_.data(),d.numSamples,0.f); }
  float peak=0.f;
  for(int32 i=0;i<d.numSamples;++i)
   peak=std::max(peak,std::max(std::fabs(left_[i]),std::fabs(right_[i])));
  outputPeakMilli_.store(uint32_t(std::min(peak,10.f)*1000.f),std::memory_order_relaxed);
  for(int32 i=0;i<d.numOutputs;++i) {
   auto& bus=d.outputs[i];
   for(int32 c=0;c<bus.numChannels;++c) {
    const float* src=c==0 ? left_.data() : right_.data();
    if(d.symbolicSampleSize==kSample32 && bus.channelBuffers32 && bus.channelBuffers32[c])
     std::copy_n(src,d.numSamples,bus.channelBuffers32[c]);
    else if(d.symbolicSampleSize==kSample64 && bus.channelBuffers64 && bus.channelBuffers64[c])
     std::copy_n(src,d.numSamples,bus.channelBuffers64[c]);
   }
   uint64_t flags=0;
   for(int32 c=0;c<bus.numChannels && c<64;++c) {
    const float* samples=c==0 ? left_.data() : right_.data();
    bool allZero=true;
    for(int32 n=0;n<d.numSamples;++n) {
     if(samples[n]!=0.f) { allZero=false; break; }
    }
    if(allZero) flags|=(uint64_t(1)<<c);
   }
   bus.silenceFlags=flags;
  }
  return kResultOk;
 }
};
class Controller final : public EditController, public IMidiMapping, public EiiiPanelSink {
public:
 uint32 PLUGIN_API addRef() override { return EditController::addRef(); }
 uint32 PLUGIN_API release() override { return EditController::release(); }
 static FUnknown* create(void*) { return (IEditController*)new Controller; }
 tresult PLUGIN_API initialize(FUnknown* ctx) override {
  const auto result=EditController::initialize(ctx);
  if(result!=kResultOk) return result;
  for(unsigned ch=0;ch<16;++ch)
   for(unsigned slot=0;slot<4;++slot) {
    const auto id=eiii::vst3::MidiControllers::id(ch,slot);
    String128 title{};
    UString(title,128).fromAscii("EIII MIDI Control");
    parameters.addParameter(title,nullptr,0,
       eiii::vst3::MidiControllers::default_normalized(slot),
       ParameterInfo::kIsHidden,id);
   }
  return kResultOk;
 }
 tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override {
  QUERY_INTERFACE(iid,obj,IMidiMapping::iid,IMidiMapping)
  return EditController::queryInterface(iid,obj);
 }
 tresult PLUGIN_API getMidiControllerAssignment(int32 bus,int16 channel,
                                                 CtrlNumber number,ParamID& id) override {
  if(bus!=0 || channel<0 || channel>15) return kResultFalse;
  unsigned slot=0;
  if(number==kCtrlModWheel) slot=0;
  else if(number==kCtrlSustainOnOff) slot=1;
  else if(number==kPitchBend) slot=2;
  else if(number==kAfterTouch) slot=3;
  else return kResultFalse;
  id=eiii::vst3::MidiControllers::id(unsigned(channel),slot);
  return kResultTrue;
 }

 std::string displayState_;
 int zoomPercent_ = eiii::vst3::zoom_default;
 eiii::vst3::DirtyDelivery dirtyDelivery_;

 bool notifyHostDirty(uint64_t revision) {
  return dirtyDelivery_.observe(revision,[this]() -> bool {
   IComponentHandler2* handler=nullptr;
   if(!componentHandler ||
      componentHandler->queryInterface(IComponentHandler2::iid,
           reinterpret_cast<void**>(&handler))!=kResultOk || !handler) return false;
   const tresult result=handler->setDirty(true);
   handler->release();
   return result==kResultOk;
  });
 }
 tresult PLUGIN_API setComponentState(IBStream* stream) override {
  if(!stream) return kInvalidArgument;
  try {
   std::vector<uint8_t> bytes;
   std::array<uint8_t,4096> buffer{};
   for(;;) {
    int32 count=0;
    const tresult result=stream->read(buffer.data(),int32(buffer.size()),&count);
    if(count<0 || count>int32(buffer.size()) ||
       bytes.size()+size_t(count)>eiii::vst3::kMaxStateBytes) return kResultFalse;
    if(count>0) bytes.insert(bytes.end(),buffer.begin(),buffer.begin()+count);
    if(count==0 || result!=kResultOk) break;
   }
   if(bytes.empty()) return kResultOk;
   eiii::vst3::DecodedState state{};
   return eiii::vst3::decode_state(bytes.data(),bytes.size(),state)
      ? kResultOk : kResultFalse;
  } catch(...) { return kResultFalse; }
 }
 tresult PLUGIN_API getState(IBStream* stream) override {
  if(!stream) return kInvalidArgument;
  if(!views_.empty() && views_.front()) displayState_=views_.front()->displayState();
  if(displayState_.size()>4096) return kResultFalse;
  const std::string data="EIII-UI-2\nzoom_percent "+std::to_string(zoomPercent_)+"\n"+displayState_;
  size_t offset=0;
  while(offset<data.size()) {
   int32 written=0;
   const int32 remaining=int32(data.size()-offset);
   const tresult result=stream->write((void*)(data.data()+offset),remaining,&written);
   if(result!=kResultOk || written<=0 || written>remaining) return kResultFalse;
   offset+=size_t(written);
  }
  return kResultOk;
 }
 tresult PLUGIN_API setState(IBStream* stream) override {
  if(!stream) return kInvalidArgument;
  std::string bytes;
  char chunk[256]{};
  for (;;) {
   int32 count=0;
   const tresult result=stream->read(chunk,int32(sizeof(chunk)),&count);
   if(count<0 || count>int32(sizeof(chunk)) || bytes.size()+size_t(count)>4096)
    return kResultFalse;
   if(count>0) bytes.append(chunk,size_t(count));
   if(count==0 || result!=kResultOk) break;
  }
  if(bytes.empty()) return kResultOk;
  const bool legacy=bytes.rfind("EIII-UI-1\n",0)==0;
  const bool current=bytes.rfind("EIII-UI-2\n",0)==0;
  if(!legacy && !current) return kResultFalse;
  const std::string next=bytes.substr(10);
  // Old header-only projects have no zoom: retain their 130% fallback.
  // EIII-UI-2 always stores exactly one strictly valid zoom line.
  if(legacy && next.empty()){
   zoomPercent_=eiii::vst3::zoom_default;
   for(auto* view:views_) if(view)view->restoreZoom(zoomPercent_);
   return kResultOk;
  }
  static constexpr const char* names[]={"stock","blue","amber","vfd","oled"};
  std::istringstream lines(next);
  std::string line,settings;
  uint32_t levels=0;
  bool gotTheme=false,gotZoom=false;
  int parsedZoom=eiii::vst3::zoom_default;
  while(std::getline(lines,line)) {
   if(line.empty()) continue;
   if(line.rfind("zoom_percent",0)==0){
    if(legacy || gotZoom || !eiii::vst3::parse_zoom_line(line,parsedZoom))
     return kResultFalse;
    gotZoom=true;
    continue;
   }
   std::istringstream entry(line);
   std::string key,name,extra;
   if(!(entry>>key>>name)) return kResultFalse;
   int index=-1;
   for(int i=0;i<5;++i) if(name==names[i]) index=i;
   if(index<0) return kResultFalse;
   if(key=="lcd_theme") {
    if(gotTheme || (entry>>extra)) return kResultFalse;
    gotTheme=true;
   } else if(key=="lcd_level") {
    double level=0.0;
    if(!(entry>>level) || (entry>>extra) || !std::isfinite(level) ||
       level<0.0 || level>1.0 || (levels & (1u<<index))) return kResultFalse;
    levels|=1u<<index;
   } else return kResultFalse;
   settings+=line+"\n";
  }
  if(current && !gotZoom) return kResultFalse;
  // No opened editor: zoom-only is a complete new-format state.
  // Otherwise preserve the old contract: a theme + exactly five levels.
  if((gotTheme && levels!=31u) || (!gotTheme && levels!=0) ||
     (legacy && !gotTheme)) return kResultFalse;
  displayState_=settings;
  zoomPercent_=parsedZoom;
  for(auto* view:views_) if(view) {
   if(gotTheme) view->restoreDisplayState(displayState_);
   view->restoreZoom(zoomPercent_);
  }
  return kResultOk;
 }
 IPlugView* PLUGIN_API createView(FIDString name) override {
  if(name && std::strcmp(name,ViewType::kEditor)==0) {
   auto* view=new EiiiPanelView(*this);
   view->restoreDisplayState(displayState_);
   view->restoreZoom(zoomPercent_);
   views_.push_back(view);
   return view;
  }
  return nullptr;
 }
 tresult PLUGIN_API notify(IMessage* message) override {
  if(message && message->getMessageID() &&
     std::strcmp(message->getMessageID(),"EIII.GUI.Dirty")==0) {
   int64 revision=0;
   if(!message->getAttributes() ||
      message->getAttributes()->getInt("revision",revision)!=kResultOk ||
      revision<=0) return kInvalidArgument;
   notifyHostDirty(uint64_t(revision));
   // Acknowledge only an actually delivered revision. The processor will
   // retry from its UI-thread timer if the handler is not connected yet.
   if(dirtyDelivery_.last_delivered()>=uint64_t(revision)) {
    IMessage* ack=allocateMessage();
    if(ack) {
     ack->setMessageID("EIII.GUI.DirtyAck");
     if(ack->getAttributes())
      ack->getAttributes()->setInt("revision",revision);
     sendMessage(ack);
     ack->release();
    }
   }
   return kResultTrue;
  }
  if(message && message->getMessageID() &&
     std::strcmp(message->getMessageID(),"EIII.GUI.Status")==0) {
   const void* data=nullptr;
   uint32 count=0;
   auto* attr=message->getAttributes();
   if(!attr || attr->getBinary("payload",data,count)!=kResultOk ||
      count!=sizeof(EiiiUiState) || !data) return kInvalidArgument;
   EiiiUiState state{};
   std::memcpy(&state,data,sizeof(state));
   // Status can re-deliver the latest revision if direct IPC arrived
   // before the host's IComponentHandler2 became available.
   notifyHostDirty(state.project_dirty_revision);
   for(auto* v:views_) if(v) v->setStatus(state);
   return kResultTrue;
  }
  return EditController::notify(message);
 }
 void requestPanelState() override {
  IMessage* m=allocateMessage();
  if(!m) return;
  m->setMessageID("EIII.GUI.Poll");
  sendMessage(m);
  m->release();
 }
 void panelButton(int index, bool down) override {
  IMessage* m=allocateMessage();
  if(!m) return;
  m->setMessageID("EIII.GUI.Control");
  m->getAttributes()->setInt("index",index);
  m->getAttributes()->setInt("down",down?1:0);
  sendMessage(m);
  m->release();
 }
 void panelMedia(int action,const char* nativePath) override {
  if(action<1 || action>7 || !nativePath) return;
  const size_t len=std::strlen(nativePath);
  if(action!=4 && action!=7 && (len==0 || len>4096)) return;
  IMessage* m=allocateMessage();
  if(!m) return;
  m->setMessageID("EIII.GUI.Media");
  m->getAttributes()->setInt("action",action);
  if(action!=4 && action!=7) m->getAttributes()->setBinary("path",nativePath,uint32(len));
  sendMessage(m);
  m->release();
 }
 void panelExportDisk(int id,const char* nativePath) override {
  if((id!=1 && (id<4 || id>7)) || !nativePath) return;
  const size_t len=std::strlen(nativePath);
  if(!len || len>4096) return;
  IMessage* m=allocateMessage();
  if(!m) return;
  m->setMessageID("EIII.GUI.ExportDisk");
  m->getAttributes()->setInt("drive",id);
  m->getAttributes()->setBinary("path",nativePath,uint32(len));
  sendMessage(m);
  m->release();
 }
 void panelDiscardDiskChanges(int id) override {
  if(id!=1 && (id<4 || id>7)) return;
  IMessage* m=allocateMessage();
  if(!m) return;
  m->setMessageID("EIII.GUI.DiscardDiskChanges");
  m->getAttributes()->setInt("drive",id);
  sendMessage(m);
  m->release();
 }
 void panelAnalogModel(int model) override {
  if(model!=1 && model!=2) return;
  IMessage* m=allocateMessage();
  if(!m) return;
  m->setMessageID("EIII.GUI.AnalogModel");
  m->getAttributes()->setInt("model",model);
  sendMessage(m);
  m->release();
 }
 void panelScsi(int id,int kind,const char* nativePath) override {
  if(id<4 || id>7 || kind<0 || kind>2 || !nativePath) return;
  const size_t len=std::strlen(nativePath);
  if((kind!=0 && (len==0 || len>4096)) || (kind==0 && len!=0)) return;
  IMessage* m=allocateMessage();
  if(!m) return;
  m->setMessageID("EIII.GUI.Scsi");
  m->getAttributes()->setInt("drive",id);
  m->getAttributes()->setInt("kind",kind);
  if(kind!=0) m->getAttributes()->setBinary("path",nativePath,uint32(len));
  sendMessage(m);
  m->release();
 }
 void panelWavImport(const char* path,unsigned root,unsigned low,unsigned high,unsigned preset) override {
  if(!path || !*path || std::strlen(path)>4096)return;
  IMessage* m=allocateMessage();if(!m)return;
  m->setMessageID("EIII.GUI.WavImport");auto* a=m->getAttributes();
  a->setBinary("path",path,uint32(std::strlen(path)));
  a->setInt("root",root);a->setInt("low",low);a->setInt("high",high);a->setInt("preset",preset);
  sendMessage(m);m->release();
 }
 void panelWavCancel() override {
  IMessage* m=allocateMessage();if(!m)return;
  m->setMessageID("EIII.GUI.WavCancel");sendMessage(m);m->release();
 }
 void panelAutoRecall(int bank,int preset) override {
  if(bank < -1 || bank > 99 || preset < -1 || preset > 99 ||
     (bank < 0 && preset >= 0)) return;
  IMessage* m=allocateMessage();
  if(!m) return;
  m->setMessageID("EIII.GUI.AutoRecall");
  m->getAttributes()->setInt("bank",bank);
  m->getAttributes()->setInt("preset",preset);
  sendMessage(m);
  m->release();
 }
 void panelMidiRouting(int channel) override {
  if (channel < 0 || channel > 16) return;
  IMessage* m=allocateMessage();
  if(!m) return;
  m->setMessageID("EIII.GUI.MidiRouting");
  m->getAttributes()->setInt("target",channel);
  sendMessage(m);
  m->release();
 }
 bool panelSlider(int index, std::uint16_t adc) override {
  IMessage* m=allocateMessage();
  if(!m) return false;
  m->setMessageID("EIII.GUI.Slider");
  m->getAttributes()->setInt("index",index);
  m->getAttributes()->setInt("adc",adc);
  const tresult accepted=sendMessage(m);
  m->release();
  return accepted==kResultOk;
 }
 void panelDisplaySettingsChanged(const std::string& settings) override {
  if(settings==displayState_) return;
  displayState_=settings;
  // Display theme/contrast are serialized in the *controller* state, not
  // in the emulated machine. Do not mutate component dirty revisions here.
  IComponentHandler2* handler=nullptr;
  if(componentHandler &&
     componentHandler->queryInterface(IComponentHandler2::iid,
          reinterpret_cast<void**>(&handler))==kResultOk && handler) {
   handler->setDirty(true);
   handler->release();
  }
 }
 void panelZoomChanged(int acceptedZoomPercent) override {
  if(!eiii::vst3::valid_zoom(acceptedZoomPercent) ||
     zoomPercent_==acceptedZoomPercent)return;
  zoomPercent_=acceptedZoomPercent;
  // A real accepted user zoom is serialized in controller state.
  IComponentHandler2* handler=nullptr;
  if(componentHandler &&
     componentHandler->queryInterface(IComponentHandler2::iid,
          reinterpret_cast<void**>(&handler))==kResultOk && handler){
   handler->setDirty(true);
   handler->release();
  }
 }
 void panelViewClosed(EiiiPanelView* view) override {
  if(view) displayState_=view->displayState();
  views_.erase(std::remove(views_.begin(),views_.end(),view),views_.end());
 }
private:
 std::vector<EiiiPanelView*> views_;
};
}
#if defined(EIII_125A_PUBLIC_VARIANT)
#define EIII_FACTORY_NAME "125A Vintage Sampler III"
#else
#define EIII_FACTORY_NAME "E-MU Emulator III"
#endif
BEGIN_FACTORY_DEF("125A Systems", "", "")
DEF_CLASS2(INLINE_UID_FROM_FUID(kProcessor), PClassInfo::kManyInstances, kVstAudioEffectClass,
 EIII_FACTORY_NAME, Vst::kDistributable, "Instrument|Sampler", "0.0.1", kVstVersionString, Processor::create)
DEF_CLASS2(INLINE_UID_FROM_FUID(kController), PClassInfo::kManyInstances, kVstComponentControllerClass,
 EIII_FACTORY_NAME " Controller", 0, "", "0.0.1", kVstVersionString, Controller::create)
END_FACTORY
