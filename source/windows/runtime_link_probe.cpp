// Firmware-free Windows runtime regression tests: not a full EIII firmware boot.
#include "../upstream/runtime/eiii_runtime.h"
#include "../upstream/runtime/eiii_runtime_impl.h"
#include "../upstream/runtime/eiii_bank_import.h"
#include "../upstream/runtime/eiii_engine.h"
#include "../upstream/standalone/shim/emu.h"
#include "../upstream/standalone/shim/image_fingerprint.h"
#include "../upstream/standalone/shim/imagedev/harddriv.h"
#include "vst3-port/memory_capture.h"
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
[[noreturn]] void shim_fatal(std::string const &);
#include <string>
#include <vector>

namespace fs = std::filesystem;
static void le(std::string &s, uint64_t v, unsigned bytes) {
    for (unsigned i = 0; i < bytes; ++i) s.push_back(char(v >> (8 * i)));
}
static uint64_t fingerprint(const std::vector<char> &v) {
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < v.size() && i < 65536; ++i) {
        h ^= static_cast<unsigned char>(v[i]);
        h *= 1099511628211ull;
    }
    return h;
}
static std::string overlay(const std::vector<char> &src, uint32_t lba) {
    std::string s = "EIIIHDC1";
    le(s, src.size(), 8); le(s, fingerprint(src), 8); le(s, 1, 4);
    le(s, lba, 4); s.append(512, char(0x5a));
    return s;
}
static std::string overlay_v2(const std::vector<char> &src, uint32_t lba) {
    eiii::shim::image_fingerprint hashes;
    hashes.update(src.data(),src.size());
    std::string s = "EIIIHDC2";
    le(s, src.size(), 8); le(s, hashes.full, 8); le(s, 1, 4);
    le(s, lba, 4); s.append(512, char(0x5a));
    return s;
}
static void write(const fs::path &p, const std::vector<char> &bytes) {
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f.write(bytes.data(), std::streamsize(bytes.size()));
}
static std::vector<char> read(const fs::path &p) {
    std::ifstream f(p, std::ios::binary);
    return std::vector<char>(std::istreambuf_iterator<char>(f), {});
}
int main() {
    std::string error;
    // A failed open still binds the candidate's shim context. Destruction
    // must not leave scheduler/address-space TLS pointing into freed storage.
    {
        auto *previous = g_shim_sched;
        auto *space = device_memory_interface::s_host_space;
        auto candidate = std::make_unique<eiii::runtime>();
        if(candidate->open({}, error)) return 130;
        candidate.reset();
        if(g_shim_sched != shim_default_scheduler() ||
           device_memory_interface::s_host_space != nullptr) return 131;
        (void)shim_sched().time();
        g_shim_sched=previous;device_memory_interface::s_host_space=space;
        // Disposing an unbound candidate must preserve another live context.
        auto bound = std::make_unique<eiii::runtime>();
        if(bound->open({},error))return 132;
        auto* bound_scheduler=g_shim_sched;
        auto* bound_space=device_memory_interface::s_host_space;
        auto other=std::make_unique<eiii::runtime>();
        other.reset();
        if(g_shim_sched!=bound_scheduler || device_memory_interface::s_host_space!=bound_space)return 133;
    }
    // B02: heap-backed snapshot fixtures also exercise the production
    // heap-backed capture/rollback path; partial transport is not live restore.
    {
        using I=eiii::runtime::impl;
        auto source = std::make_unique<I::core_recall_state>();
        source->motherboard_ram.assign(0x10000, 0x1234);
        source->sample_ram.assign(0x400000, 0x5678);
        source->midi.q={0x90,0x3c,0x7f};
        source->midi.ev={{0.25,{0x80,0x3c,0}}};
        std::vector<uint8_t> encoded;
        if(!I::encode_partial_core_recall(*source,encoded)) return 40;
        std::vector<uint16_t> ram,samples;
        I::midi_tx_t midi;
        if(!I::decode_partial_core_recall(encoded.data(),encoded.size(),ram,samples,midi) ||
           ram!=source->motherboard_ram || samples!=source->sample_ram ||
           midi.q!=source->midi.q || midi.ev!=source->midi.ev) return 41;
        auto base = std::make_unique<I::core_recall_state>();
        auto staged = std::make_unique<I::core_recall_state>();
        base->cpu.pc=0xabcdef;
        staged->cpu.pc=0x111111;
        if(!I::stage_partial_core_recall(encoded.data(),encoded.size(),*base,*staged) ||
           staged->cpu.pc!=base->cpu.pc ||
           staged->motherboard_ram!=source->motherboard_ram ||
           staged->sample_ram!=source->sample_ram ||
           staged->midi.ev!=source->midi.ev) return 43;
        // B02 CPU execution subsection must stage independently of live state.
        source->cpu.pc=0x76543210;
        source->cpu_clock.completed_cycles=0x123456789ULL;
        // Single envelope carries RAM, MIDI and CPU position together.
        std::vector<uint8_t> checkpointWire;
        if(!I::encode_checkpoint_bundle(*source,checkpointWire)) return 48;
        std::vector<uint16_t> bundleRam,bundleSamples;
        eiii::vst3::CoreMidiSection::State bundleMidi;
        eiii::vst3::CoreExecutionSection::State bundleCpu;
        if(!eiii::vst3::CoreCheckpointBundle::decode(checkpointWire.data(),
            checkpointWire.size(),bundleRam,bundleSamples,bundleMidi,bundleCpu) ||
           bundleRam!=source->motherboard_ram ||
           bundleSamples!=source->sample_ram ||
           bundleMidi.queue!=source->midi.q ||
           bundleCpu.pc!=source->cpu.pc ||
           bundleCpu.completed_cycles!=source->cpu_clock.completed_cycles) return 49;
        auto stagedBundle = std::make_unique<I::core_recall_state>();
        auto bundleBase = std::make_unique<I::core_recall_state>();
        bundleBase->cpu.pc=0x11;
        if(!I::stage_checkpoint_bundle(checkpointWire.data(),checkpointWire.size(),
             *bundleBase,*stagedBundle) ||
           stagedBundle->cpu.pc!=source->cpu.pc ||
           stagedBundle->cpu_clock.completed_cycles!=source->cpu_clock.completed_cycles ||
           stagedBundle->motherboard_ram!=source->motherboard_ram ||
           stagedBundle->midi.ev!=source->midi.ev) return 51;
        const auto bundleSavedPc=stagedBundle->cpu.pc;
        checkpointWire.back()^=1;
        if(I::stage_checkpoint_bundle(checkpointWire.data(),checkpointWire.size(),
             *bundleBase,*stagedBundle) || stagedBundle->cpu.pc!=bundleSavedPc) return 52;
        if(eiii::vst3::CoreCheckpointBundle::decode(checkpointWire.data(),
            checkpointWire.size(),bundleRam,bundleSamples,bundleMidi,bundleCpu) ||
           bundleCpu.pc!=source->cpu.pc) return 50;
        // Complete NS32016 register-file roundtrip, including input lines.
        source->cpu.ssp=0x11223344;
        source->cpu.r[7]=0x98765432;
        source->cpu.psr=0x1234;
        source->cpu.ready=true;
        source->cpu.wait=true;
        source->fpu.fsr=0x12345678;
        source->fpu.op[0].expected=4;
        source->fpu.op[0].issued=2;
        source->fpu.op[0].value=0x123456789ULL;
        source->scheduler.basetime=attotime::from_usec(300);
        source->scheduler.timers.push_back({attotime::from_usec(900),
            attotime::from_usec(300),attotime::from_usec(100),7,true});
        source->ncr.phase=0;
        source->ncr.scsi_ctrl=0;
        source->ncr.odata=0x92;
        source->ncr.rst_state=true;
        source->scsi_bus.device_count=2;
        source->scsi_bus.ctrl=0x123;
        source->scsi_bus.data=0x456;
        source->scsi_bus.devices[0]={"controller",1,2,3};
        source->scsi_bus.devices[1]={"disk1",4,5,6};
        nscsi_full_device::target_recall_state target{};
        target.command[0]=0x28;
        target.command_size=10;
        target.queue_read=0;
        target.queue_write=1;
        target.actions[0].action=2;
        nscsi_harddisk_device::disk_target_recall_state hdd{};
        hdd.lba=123;hdd.cur_lba=122;hdd.media_present=true;
        hdd.media_length=8192;hdd.media_fingerprint=0x123456789ULL;
        hdd.sector[0]=0xaa;hdd.inquiry={1,2,3};hdd.model_name="TestDisk";
        std::vector<uint8_t> hddWire;
        nscsi_harddisk_device::disk_target_recall_state restoredHdd{};
        if(!I::encode_hdd_specific(hdd,hddWire) ||
           !I::stage_hdd_specific(hddWire.data(),hddWire.size(),restoredHdd) ||
           restoredHdd.media_fingerprint!=hdd.media_fingerprint ||
           restoredHdd.sector[0]!=0xaa || restoredHdd.lba!=123) return 65;
        hddWire.back()^=1;
        if(I::stage_hdd_specific(hddWire.data(),hddWire.size(),restoredHdd) ||
           restoredHdd.media_fingerprint!=hdd.media_fingerprint) return 66;
        nscsi_cdrom_device::cd_target_recall_state cdState{};
        cdState.media_present=true;
        cdState.media_length=4096;
        cdState.media_fingerprint=0x1234567890ULL;
        cdState.transfer={1,2,3,4};
        cdState.transfer_position=2;
        // B02 section packer integrates the real value-owned subdevice states.
        // The synthetic fixture has no attached media; its core fields are valid.
        source->disk_present.fill(false);
        source->cd_present.fill(false);
        source->scc_a_fifo.cursors={0,1,9,0,1,4};
        source->scc_b_fifo.cursors={0,1,9,0,1,4};
        source->motherboard.lcd_ac=12;
        source->motherboard.lcd_dir=1;
        source->motherboard.lcd_num_line=2;
        source->motherboard.lcd_ddram[0]=0x48;
        source->motherboard.scan_head=0;
        source->motherboard.scan_tail=1;
        source->motherboard.scan_count=1;
        source->motherboard.scan_fifo[0]=0x52;
        source->motherboard.pit[0].clock_hz=1000000;
        source->motherboard.pit[0].mode=3;
        source->audio.rate=44100;source->audio.sample_rate=44100;
        source->audio.oversample=1;source->audio.sample_words=source->sample_ram.size();
        source->audio.now=123456;source->audio.next=123456;
        source->audio.voices[2].rf[0]=0x1234;
        source->audio.voices[2].rate=128;
        source->audio.voices[2].active=true;
        // V1 has no active FIR filter; the mandatory section must record
        // the explicit inactive marker rather than failing serialization.
        std::vector<uint8_t> inactiveFir;
        if(!I::encode_fir_recall(*source,inactiveFir))return 81;
        auto firStaged=std::make_unique<I::core_recall_state>(*source);
        if(!I::stage_fir_recall(inactiveFir.data(),inactiveFir.size(),*firStaged))return 82;
        source->audio.cem_v1.voice(3).set_cv(2,0x3000);
        source->audio.output={0.25f,-0.5f};source->audio.output_frames=1;source->audio.cv[3][4]=0x1234;
        std::vector<uint8_t> allSections;
        std::vector<eiii::vst3::CoreSectionContainer::Section> sectionList;
        if(!I::encode_core_device_sections(*source,allSections) ||
           !eiii::vst3::CoreSectionContainer::decode(allSections.data(),
                   allSections.size(),sectionList) ||
           sectionList.size()!=23 || sectionList[0].id!=1 ||
           sectionList[22].id!=23) return 69;
        auto stagedSections=std::make_unique<I::core_recall_state>();
        if(!I::stage_core_device_sections(allSections.data(),allSections.size(),
                                         *source,*stagedSections) ||
           stagedSections->cpu.pc!=source->cpu.pc ||
           stagedSections->motherboard.lcd_ddram[0]!=0x48 ||
           stagedSections->motherboard.scan_fifo[0]!=0x52 ||
           stagedSections->motherboard.pit[0].mode!=3 ||
           stagedSections->audio.voices[2].rf[0]!=0x1234 ||
           stagedSections->audio.voices[2].rate!=128 ||
           stagedSections->audio.now!=123456 ||
           stagedSections->audio.output!=source->audio.output ||
           stagedSections->audio.cv!=source->audio.cv ||
           stagedSections->audio.sample_words!=source->sample_ram.size() ||
           stagedSections->audio.cem_v1.voice(3).capture_recall_state().values[7]!=
             source->audio.cem_v1.voice(3).capture_recall_state().values[7] ||
           !stagedSections->audio.voices[2].active ||
           stagedSections->disk_present!=source->disk_present)return 71;
        // A malformed pending stereo queue must never yield a checkpoint.
        source->audio.output.push_back(0.75f);
        std::vector<uint8_t> rejectedAudio;
        if(I::encode_core_device_sections(*source,rejectedAudio))return 83;
        source->audio.output.pop_back();
        // Reject a checkpoint when the destination's audio model disagrees.
        auto incompatible=std::make_unique<I::core_recall_state>(*source);
        incompatible->audio.model_v2=true;
        auto unchanged=std::make_unique<I::core_recall_state>();
        if(I::stage_core_device_sections(allSections.data(),allSections.size(),
                                         *incompatible,*unchanged))return 84;
        source->scc_a_regs.rr[0]=0x31;
        source->scc_b_regs.wr[15]=0x42;
        std::vector<uint8_t> sccWire;
        if(!I::encode_scc_registers(*source,sccWire) ||
           !I::stage_scc_registers(sccWire.data(),sccWire.size(),*stagedSections) ||
           stagedSections->scc_a_regs.rr[0]!=0x31 ||
           stagedSections->scc_b_regs.wr[15]!=0x42)return 73;
        source->scc_controller.wr9=0x80;
        source->scc_controller.int_state[2]=1;
        std::vector<uint8_t> sccCtrlWire;
        if(!I::encode_scc_controller(*source,sccCtrlWire) ||
           !I::stage_scc_controller(sccCtrlWire.data(),sccCtrlWire.size(),*stagedSections) ||
           stagedSections->scc_controller.wr9!=0x80 ||
           stagedSections->scc_controller.int_state[2]!=1)return 74;
        source->scc_a_fifo.cursors={0,1,9,0,1,4};
        source->scc_b_fifo.cursors={0,1,9,0,1,4};
        source->scc_a_fifo.rx_data[0]=0x51;
        std::vector<uint8_t> fifoWire;
        if(!I::encode_scc_fifo(*source,fifoWire) ||
           !I::stage_scc_fifo(fifoWire.data(),fifoWire.size(),*stagedSections) ||
           stagedSections->scc_a_fifo.rx_data[0]!=0x51)return 75;
        source->motherboard.lcd_ac=12;
        source->motherboard.lcd_dir=1;
        source->motherboard.lcd_num_line=2;
        source->motherboard.lcd_ddram[0]=0x48;
        std::vector<uint8_t> lcdWire;
        if(!I::encode_motherboard_lcd(*source,lcdWire) ||
           !I::stage_motherboard_lcd(lcdWire.data(),lcdWire.size(),*stagedSections) ||
           stagedSections->motherboard.lcd_ddram[0]!=0x48 ||
           stagedSections->motherboard.lcd_ac!=12)return 76;
        source->motherboard.scan_head=0;
        source->motherboard.scan_tail=1;
        source->motherboard.scan_count=1;
        source->motherboard.scan_fifo[0]=0x52;
        std::vector<uint8_t> panelWire;
        if(!I::encode_motherboard_panel(*source,panelWire) ||
           !I::stage_motherboard_panel(panelWire.data(),panelWire.size(),*stagedSections) ||
           stagedSections->motherboard.scan_fifo[0]!=0x52 ||
           stagedSections->motherboard.scan_count!=1)return 77;
        source->motherboard.dig_words[1]=0x55aa;
        source->motherboard.cv_words[3]=0x1234;
        std::vector<uint8_t> mbWire;
        if(!I::encode_motherboard_registers(*source,mbWire) ||
           !I::stage_motherboard_registers(mbWire.data(),mbWire.size(),*stagedSections) ||
           stagedSections->motherboard.dig_words[1]!=0x55aa ||
           stagedSections->motherboard.cv_words[3]!=0x1234)return 78;
        allSections.back()^=1;
        if(eiii::vst3::CoreSectionContainer::decode(allSections.data(),
             allSections.size(),sectionList) || sectionList.size()!=23) return 70;
        if(I::stage_core_device_sections(allSections.data(),allSections.size(),
             *source,*stagedSections) || stagedSections->cpu.pc!=source->cpu.pc) return 72;
        // Audio workstream: 16 F-Chip voice registers, independently staged.
        // Not yet part of full-machine serialization (CEM/FIR still missing).
        source->audio.voices[2].rf[0]=0x1234;
        source->audio.voices[2].rate=128;
        source->audio.voices[2].active=true;
        std::vector<uint8_t> fchipWire;
        auto fchipStaged=std::make_unique<I::core_recall_state>(*source);
        fchipStaged->audio.voices[2]={};
        if(!I::encode_fchip_voices(*source,fchipWire) ||
           !I::stage_fchip_voices(fchipWire.data(),fchipWire.size(),*fchipStaged) ||
           fchipStaged->audio.voices[2].rf[0]!=0x1234 ||
           fchipStaged->audio.voices[2].rate!=128 ||
           !fchipStaged->audio.voices[2].active)return 80;
        // Exercise the full section roster with *both* actual target types.
        // Earlier bundle coverage used only the empty-media topology.
        auto mediaSource=std::make_unique<I::core_recall_state>(*source);
        mediaSource->disk_present[1]=true;
        mediaSource->cd_present[3]=true;
        mediaSource->disks[1].sector[0]=0xa6;
        mediaSource->disks[1].lba=42;
        mediaSource->cds[3].sector[0]=0xc7;
        mediaSource->cds[3].lba=84;
        std::vector<uint8_t> mediaBundle;
        auto mediaStaged=std::make_unique<I::core_recall_state>();
        if(!I::encode_core_device_sections(*mediaSource,mediaBundle) ||
           !I::stage_core_device_sections(mediaBundle.data(),mediaBundle.size(),
                                          *mediaSource,*mediaStaged) ||
           !mediaStaged->disk_present[1] || !mediaStaged->cd_present[3] ||
           mediaStaged->disks[1].sector[0]!=0xa6 ||
           mediaStaged->cds[3].sector[0]!=0xc7 ||
           mediaStaged->disks[1].lba!=42 ||
           mediaStaged->cds[3].lba!=84)return 79;
        std::vector<uint8_t> cdWire;
        nscsi_cdrom_device::cd_target_recall_state restoredCd{};
        if(!I::encode_cd_specific(cdState,cdWire) ||
           !I::stage_cd_specific(cdWire.data(),cdWire.size(),restoredCd) ||
           restoredCd.media_fingerprint!=cdState.media_fingerprint ||
           restoredCd.transfer_position!=2) return 67;
        cdWire.back()^=1;
        if(I::stage_cd_specific(cdWire.data(),cdWire.size(),restoredCd) ||
           restoredCd.transfer_position!=2) return 68;
        std::vector<uint8_t> targetWire;
        nscsi_full_device::target_recall_state restoredTarget{};
        if(!I::encode_scsi_target(target,targetWire) ||
           !I::decode_scsi_target(targetWire.data(),targetWire.size(),restoredTarget) ||
           restoredTarget.command[0]!=0x28 ||
           restoredTarget.actions[0].action!=2) return 63;
        targetWire.back()^=1;
        if(I::decode_scsi_target(targetWire.data(),targetWire.size(),restoredTarget) ||
           restoredTarget.command[0]!=0x28) return 64;
        std::vector<uint8_t> scsiBusWire;
        if(!I::encode_scsi_bus_recall(*source,scsiBusWire) ||
           !I::stage_scsi_bus_recall(scsiBusWire.data(),scsiBusWire.size(),*staged) ||
           staged->scsi_bus.device_count!=2 ||
           staged->scsi_bus.devices[1].tag!="disk1" ||
           staged->scsi_bus.devices[1].wait_ctrl!=5) return 61;
        scsiBusWire.back()^=1;
        if(I::stage_scsi_bus_recall(scsiBusWire.data(),scsiBusWire.size(),*staged) ||
           staged->scsi_bus.devices[1].tag!="disk1") return 62;
        std::vector<uint8_t> ncrWire;
        if(!I::encode_ncr_recall(*source,ncrWire) ||
           !I::stage_ncr_recall(ncrWire.data(),ncrWire.size(),*staged) ||
           staged->ncr.odata!=source->ncr.odata ||
           !staged->ncr.rst_state) return 59;
        ncrWire.back()^=1;
        if(I::stage_ncr_recall(ncrWire.data(),ncrWire.size(),*staged) ||
           staged->ncr.odata!=source->ncr.odata) return 60;
        std::vector<uint8_t> schedulerWire;
        if(!I::encode_scheduler_recall(*source,schedulerWire) ||
           !I::stage_scheduler_recall(schedulerWire.data(),schedulerWire.size(),*staged) ||
           staged->scheduler.basetime!=source->scheduler.basetime ||
           staged->scheduler.timers.size()!=1 ||
           staged->scheduler.timers[0].param!=7) return 57;
        // Pre-D0 B02 project: five fewer timers. Only migrate when the new
        // drive's index timer and four WD1772 timers are proven to form
        // an exact tail of the current machine's timer registration order.
        {
            auto noProof=std::make_unique<I::core_recall_state>();
            noProof->scheduler.timers.resize(6);
            if(I::stage_scheduler_recall(schedulerWire.data(),schedulerWire.size(),*noProof))
                return 89;
            auto oldProject=std::make_unique<I::core_recall_state>();
            oldProject->scheduler.timers.resize(6);
            oldProject->d0_timers_at_tail=true;
            if(!I::stage_scheduler_recall(schedulerWire.data(),schedulerWire.size(),*oldProject) ||
               oldProject->scheduler.timers.size()!=6 ||
               !oldProject->migrated_legacy_d0_timers ||
               oldProject->scheduler.timers[0].param!=7 ||
               oldProject->scheduler.timers[5].enabled)
                return 90;
            auto mountedD0=std::make_unique<I::core_recall_state>();
            mountedD0->scheduler.timers.resize(6);
            mountedD0->d0_timers_at_tail=true;
            mountedD0->d0_present=true;
            if(I::stage_scheduler_recall(schedulerWire.data(),schedulerWire.size(),*mountedD0))
                return 91;
        }
        schedulerWire.back()^=1;
        if(I::stage_scheduler_recall(schedulerWire.data(),schedulerWire.size(),*staged) ||
           staged->scheduler.timers.size()!=1) return 58;
        std::vector<uint8_t> fpuWire;
        if(!I::encode_fpu_recall(*source,fpuWire) ||
           !I::stage_fpu_recall(fpuWire.data(),fpuWire.size(),*staged) ||
           staged->fpu.fsr!=source->fpu.fsr ||
           staged->fpu.op[0].value!=source->fpu.op[0].value) return 55;
        fpuWire.back()^=1;
        if(I::stage_fpu_recall(fpuWire.data(),fpuWire.size(),*staged) ||
           staged->fpu.fsr!=source->fpu.fsr) return 56;
        std::vector<uint8_t> registersWire;
        if(!I::encode_cpu_registers(*source,registersWire) ||
           !I::stage_cpu_registers(registersWire.data(),registersWire.size(),*staged) ||
           staged->cpu.ssp!=source->cpu.ssp ||
           staged->cpu.r[7]!=source->cpu.r[7] ||
           staged->cpu.psr!=source->cpu.psr ||
           !staged->cpu.ready || !staged->cpu.wait) return 53;
        registersWire.back()^=1;
        const auto preservedRegister=staged->cpu.r[7];
        if(I::stage_cpu_registers(registersWire.data(),registersWire.size(),*staged) ||
           staged->cpu.r[7]!=preservedRegister) return 54;
        std::vector<uint8_t> cpuWire;
        if(!I::encode_execution_core_recall(*source,cpuWire) ||
           !I::stage_execution_core_recall(cpuWire.data(),cpuWire.size(),*staged) ||
           staged->cpu.pc!=source->cpu.pc ||
           staged->cpu_clock.completed_cycles!=source->cpu_clock.completed_cycles) return 46;
        cpuWire.back()^=1;
        const auto cpuPcBefore=staged->cpu.pc;
        const auto cpuCyclesBefore=staged->cpu_clock.completed_cycles;
        if(I::stage_execution_core_recall(cpuWire.data(),cpuWire.size(),*staged) ||
           staged->cpu.pc!=cpuPcBefore ||
           staged->cpu_clock.completed_cycles!=cpuCyclesBefore) return 47;
        const auto oldPc=staged->cpu.pc;
        if(staged->midi.q_head!=source->midi.q_head || staged->midi.bit!=source->midi.bit) return 45;
        encoded.back()^=1;
        if(I::stage_partial_core_recall(encoded.data(),encoded.size(),*base,*staged) ||
           staged->cpu.pc!=oldPc || staged->midi.q!=source->midi.q) return 44;
        // Keep the payload corrupted for the decoder's rejection check.
        if(I::decode_partial_core_recall(encoded.data(),encoded.size(),ram,samples,midi) ||
           ram!=source->motherboard_ram || midi.q!=source->midi.q) return 42;
    }

    // B02: cross-device checkpoint comparator must reject MIDI/CPU drift.
    {
        using impl=eiii::runtime::impl;
        auto left = std::make_unique<impl::core_recall_state>();
        auto right = std::make_unique<impl::core_recall_state>();
        left->motherboard_ram.resize(0x10000);
        left->sample_ram.resize(0x400000);
        left->motherboard.lcd_dir=1;left->motherboard.lcd_num_line=1;
        left->scc_a_fifo.cursors={0,0,9,0,0,4};
        left->scc_b_fifo.cursors={0,0,9,0,0,4};
        left->audio.rate=44100;left->audio.sample_rate=44100;
        left->audio.oversample=1;left->audio.sample_words=0x400000;
        left->scheduler.basetime=attotime::from_usec(500);
        left->cpu.pc=0x1234;
        left->cpu_clock.completed_cycles=500;
        left->midi.q={0x90,0x3c,0x7f};
        *right=*left;
        if(!impl::same_core_checkpoint(*left,*right)) return 36;
        right->midi.q[2]=0x40;
        if(impl::same_core_checkpoint(*left,*right)) return 37;
        *right=*left;
        ++right->cpu_clock.completed_cycles;
        if(impl::same_core_checkpoint(*left,*right)) return 38;
        *right=*left;
        right->scheduler.basetime=attotime::from_usec(501);
        if(impl::same_core_checkpoint(*left,*right)) return 39;
        *right=*left;++right->cpu.r[3];
        if(impl::same_core_checkpoint(*left,*right)) return 85;
        *right=*left;right->serial[0].rcv_register_data^=1;
        if(impl::same_core_checkpoint(*left,*right)) return 86;
        *right=*left;right->context.pit1_last=!left->context.pit1_last;
        if(impl::same_core_checkpoint(*left,*right)) return 87;
    }
    // B02 scheduler checkpoint equality must detect timer-only drift.
    {
        using impl=eiii::runtime::impl;
        shim_scheduler::recall_state lhs{}, rhs{};
        lhs.basetime=attotime::from_usec(500);
        lhs.timers.push_back({attotime::from_usec(900),attotime::from_usec(500),attotime::from_usec(200),7,true});
        rhs=lhs;
        if(!impl::same_scheduler_checkpoint(lhs,rhs)) return 33;
        rhs.timers[0].param=8;
        if(impl::same_scheduler_checkpoint(lhs,rhs)) return 34;
        rhs=lhs;
        rhs.basetime=attotime::from_usec(501);
        if(impl::same_scheduler_checkpoint(lhs,rhs)) return 35;
    }
    // B02: two unpowered instances must reject restore without mutation.
    {
        auto first=std::make_unique<eiii::runtime::impl>();
        auto second=std::make_unique<eiii::runtime::impl>();
        first->m.ram[4]=0x1122;
        second->m.ram[4]=0x5566;
        auto state = std::make_unique<eiii::runtime::impl::core_recall_state>();
        if(first->capture_core_recall(*state) || second->restore_core_recall(*state) ||
           first->m.ram[4]!=0x1122 || second->m.ram[4]!=0x5566) return 32;
    }
    if (eiii::runtime::check_rom_dir("125A-THIS-FOLDER-DOES-NOT-EXIST", error) || error.empty()) {
        std::cerr << "FAIL: missing ROM diagnostic\n";
        return 1;
    }
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path dir = fs::temp_directory_path() / ("eiii_disk_export_test_" + std::to_string(nonce));
    fs::create_directories(dir);
    struct Cleanup { fs::path dir; ~Cleanup() { std::error_code ec; fs::remove_all(dir, ec); } } cleanup{dir};
    const fs::path source = dir / "source.img", output = dir / "export.img";
    const std::vector<char> initial(4096, char(0x23));
    write(source, initial);
    // B12: reject malformed raw HDD and CD-ROM sectors before boot.
    if(!eiii::runtime::check_hd_image(source.string(),error)) {
        std::cerr << "FAIL: valid HDD image rejected: " << error << "\\n";
        return 9;
    }
    const fs::path invalidHd = dir / "invalid.img", validCd = dir / "disc.iso";
    write(invalidHd, std::vector<char>(513, char(0x42)));
    error.clear();
    if(eiii::runtime::check_hd_image(invalidHd.string(),error) || error.empty()) {
        std::cerr << "FAIL: malformed HDD image accepted\\n";return 10;
    }
    error.clear();
    if(eiii::runtime::check_cd_image(invalidHd.string(),error) || error.empty()) {
        std::cerr << "FAIL: malformed CD image accepted\\n";return 11;
    }
    write(validCd, std::vector<char>(2048, char(0x23)));
    error.clear();
    if(!eiii::runtime::check_cd_image(validCd.string(),error)) {
        std::cerr << "FAIL: valid CD image rejected: " << error << "\\n";return 12;
    }
    const std::string delta = overlay(initial, 3);
    if (!eiii::runtime::write_image_with_changes(source.string(), delta, output.string(), error)) {
        std::cerr << "FAIL: valid overlay export: " << error << "\n";
        return 2;
    }
    auto result = read(output);
    if (result.size() != initial.size() || result[3*512] != char(0x5a) ||
        result[2*512] != char(0x23) || read(source) != initial) {
        std::cerr << "FAIL: sector content or original disk modified\n";
        return 3;
    }
    error.clear();
    if (eiii::runtime::write_image_with_changes(source.string(), delta, output.string(), error) ||
        read(output) != result) {
        std::cerr << "FAIL: existing target was overwritten\n";
        return 4;
    }
    fs::remove(output);
    auto altered = initial; altered[0] ^= 1;
    write(source, altered);
    error.clear();
    if (eiii::runtime::write_image_with_changes(source.string(), delta, output.string(), error) ||
        fs::exists(output)) {
        std::cerr << "FAIL: changed source fingerprint accepted\n";
        return 5;
    }
    write(source, initial);
    error.clear();
    if (eiii::runtime::write_image_with_changes(source.string(), overlay(initial, UINT32_MAX), output.string(), error) ||
        fs::exists(output)) {
        std::cerr << "FAIL: out-of-range LBA accepted\n";
        return 6;
    }
    // B16: a truncated overlay must be rejected without leaving a partial export.
    {
        std::string truncated = delta.substr(0, delta.size() - 1);
        error.clear();
        if (eiii::runtime::write_image_with_changes(source.string(), truncated, output.string(), error) ||
            fs::exists(output)) {
            std::cerr << "FAIL B16: truncated change set accepted or output left behind\n";
            return 46;
        }
        std::string invalidSector = overlay(initial, 8); // 4096 bytes = sectors 0..7
        error.clear();
        if (eiii::runtime::write_image_with_changes(source.string(), invalidSector, output.string(), error) ||
            fs::exists(output)) {
            std::cerr << "FAIL B16: first invalid LBA accepted or partial output left behind\n";
            return 47;
        }
    }
    // A03: two equal-length images with identical first 64 KiB must not
    // share a new-format overlay merely because their system areas match.
    // Old HDC1 state remains readable but retains an explicit legacy warning.
    {
        const fs::path diskA=dir/"a03-disk.img", diskB=dir/"a03-other.img";
        const fs::path diskOut=dir/"a03-reconstructed.img";
        std::vector<char> first(131072,char(0x23)),other=first;
        other[70000]^=0x7f;
        write(diskA,first);
        write(diskB,other);
        const auto strong=overlay_v2(first,3),legacy=overlay(first,3);
        error.clear();
        if (!eiii::runtime::write_image_with_changes(diskA.string(), strong, diskOut.string(), error)) {
            std::cerr<<"FAIL A03: valid strong overlay refused: "<<error<<"\n"; return 41;
        }
        fs::remove(diskOut);
        error.clear();
        if (eiii::runtime::write_image_with_changes(diskB.string(), strong, diskOut.string(), error) ||
            fs::exists(diskOut)) {
            std::cerr<<"FAIL A03: same-prefix different disk accepted strong overlay\n"; return 42;
        }
        machine_config mc;
        {
            harddisk_image_device good(mc,"a03_good",nullptr);
            if (!good.load(diskA.string()) || !good.import_changes(strong,error) ||
                good.export_changes().compare(0,8,"EIIIHDC2")!=0) {
                std::cerr<<"FAIL A03: HDC2 source import/roundtrip\n"; return 43;
            }
        }
        {
            harddisk_image_device changed(mc,"a03_changed",nullptr);
            if (!changed.load(diskB.string()) || changed.import_changes(strong,error) ||
                error.find("different disk") == std::string::npos) {
                std::cerr<<"FAIL A03: changed source HDC2 rejected incorrectly\n"; return 44;
            }
            error.clear();
            if (!changed.import_changes(legacy,error) || !changed.legacy_overlay() ||
                changed.export_changes().compare(0,8,"EIIIHDC1")!=0 ||
                error.find("64 KiB") == std::string::npos) {
                std::cerr<<"FAIL A03: HDC1 compatibility/warning or tag preservation\n"; return 45;
            }
        }
    }
    bool caught=false;
    try { shim_fatal("R02 regression"); }
    catch (const std::runtime_error &ex) {
        caught=std::string(ex.what()).find("R02 regression")!=std::string::npos;
    }
    if (!caught) { std::cerr << "FAIL: fatal failure not caught\n"; return 7; }
    // B02: a cold runtime and a stopped engine must reject capture.
    // Failure must leave caller-owned buffers unchanged.
    {
        eiii::runtime cold;
        std::vector<uint16_t> ram{0x1111}, samples{0x2222};
        if (cold.copy_volatile_memory(ram,samples) ||
            ram != std::vector<uint16_t>{0x1111} ||
            samples != std::vector<uint16_t>{0x2222}) {
            std::cerr << "FAIL: B02 cold runtime returned or modified memory\\n";
            return 13;
        }
        eiii::engine stopped;
        std::vector<uint8_t> encoded{0x12,0x34,0x56};
        if (eiii::vst3::MemoryCapture::capture(stopped,encoded) ||
            encoded != std::vector<uint8_t>{0x12,0x34,0x56}) {
            std::cerr << "FAIL: B02 composed capture changed output for stopped engine\\n";
            return 15;
        }
        if (stopped.capture_volatile_memory(ram,samples) ||
            ram != std::vector<uint16_t>{0x1111} ||
            samples != std::vector<uint16_t>{0x2222}) {
            std::cerr << "FAIL: B02 stopped engine returned or modified memory\\n";
            return 14;
        }
    }
    // B02: scheduler and all timers restore together, without callbacks firing.
    // Also check all-or-nothing refusal on malformed timer count and deadlines.
    {
        shim_scheduler local;
        shim_scheduler *prior = g_shim_sched;
        g_shim_sched = &local;
        {
            int fired = 0;
            emu_timer first([&](s32) { ++fired; });
            emu_timer second([&](s32) { ++fired; });
            first.adjust(attotime::from_usec(100));
            second.adjust(attotime::from_usec(250), 9, attotime::from_usec(250));
            shim_scheduler::recall_state saved;
            if (!local.capture_recall_state(saved) || saved.timers.size() != 2) {
                std::cerr << "FAIL: B02 scheduler capture\\n"; return 16;
            }
            first.adjust(attotime::never);
            shim_scheduler::recall_state parked;
            if(!local.capture_recall_state(parked) || !parked.timers[0].enabled ||
               !local.restore_recall_state(parked) || !first.enabled() || !first.expire().is_never()) {
                std::cerr<<"FAIL: B02 original adjust(never) roundtrip\n";return 88;
            }
            first.adjust(attotime::from_usec(500));
            second.enable(false);
            auto wrong = saved;
            wrong.timers.pop_back();
            if (local.restore_recall_state(wrong) || second.enabled()) {
                // The rejected restore must not reactivate the disabled timer.
                std::cerr << "FAIL: B02 timer-count rejection\\n"; return 17;
            }
            wrong = saved;
            wrong.timers[0].expire = attotime(std::numeric_limits<attotime::T>::quiet_NaN());
            if (local.restore_recall_state(wrong)) {
                std::cerr << "FAIL: B02 invalid timer deadline accepted\\n"; return 18;
            }
            // Running-CPU guard: both operations must refuse and preserve
            // the previously mutated (not saved) timer state.
            local.m_cpu_running = true;
            shim_scheduler::recall_state untouched;
            untouched.basetime = attotime::from_usec(42);
            if (local.capture_recall_state(untouched) ||
                untouched.basetime != attotime::from_usec(42) ||
                local.restore_recall_state(saved) || second.enabled() ||
                first.expire() != attotime::from_usec(500)) {
                std::cerr << "FAIL: B02 running CPU guard\\n"; return 21;
            }
            local.m_cpu_running = false;
            wrong = saved;
            wrong.basetime = attotime::never;
            if (local.restore_recall_state(wrong) || second.enabled()) {
                std::cerr << "FAIL: B02 invalid scheduler time accepted\\n"; return 22;
            }
            if (!local.restore_recall_state(saved) || !second.enabled() ||
                first.expire() != saved.timers[0].expire ||
                second.expire() != saved.timers[1].expire ||
                fired != 0) {
                std::cerr << "FAIL: B02 scheduler restore mismatch\\n"; return 19;
            }
            local.fire_due(attotime::from_usec(100));
            if (fired != 1) {
                std::cerr << "FAIL: B02 restored timer failed to fire\\n"; return 20;
            }
        }
        g_shim_sched = prior;
    }
    // B02: the internal combined core API must reject an unopened runtime
    // without mutating output. No proprietary ROM or OS fixture required.
    {
        eiii::runtime cold;
        auto &impl = cold.internals();
        auto snapshot = std::make_unique<eiii::runtime::impl::core_recall_state>();
        snapshot->cpu.pc = 0x12345678;
        if (impl.capture_core_recall(*snapshot) || snapshot->cpu.pc != 0x12345678 ||
            impl.restore_core_recall(*snapshot)) {
            std::cerr << "FAIL: B02 cold combined core recall guard\\n";
            return 23;
        }
    }
    // B02 SCC channel FIFO cursor bounds (no firmware required).
    {
        using channel = z80scc_channel;
        channel::fifo_recall_cursors c{0, 1, 9, 0, 1, 4};
        if (!channel::valid_fifo_cursors(c)) {
            std::cerr << "FAIL: B02 valid SCC FIFO cursors rejected\n";
            return 24;
        }
        c.rx_write = 9;
        if (channel::valid_fifo_cursors(c)) {
            std::cerr << "FAIL: B02 SCC RX FIFO overrun cursor accepted\n";
            return 25;
        }
        c.rx_write = 1; c.tx_capacity = 5;
        if (channel::valid_fifo_cursors(c)) {
            std::cerr << "FAIL: B02 SCC TX FIFO oversize accepted\n";
            return 26;
        }
        c.tx_capacity = 4; c.tx_read = -1;
        if (channel::valid_fifo_cursors(c)) {
            std::cerr << "FAIL: B02 SCC negative cursor accepted\n";
            return 27;
        }
    }
    // B02 SCC controller validation: IRQ request/in-service bits are valid,
    // but malformed IRQ state must never reach the shared restore.
    {
        z80scc_device::controller_recall_state c{};
        c.int_state[0] = 3;
        if (!z80scc_device::valid_controller_recall(c)) {
            std::cerr << "FAIL: B02 SCC in-service IRQ rejected\n"; return 28;
        }
        c.int_state[0] = 4;
        if (z80scc_device::valid_controller_recall(c)) {
            std::cerr << "FAIL: B02 SCC corrupt IRQ bits accepted\n"; return 29;
        }
        c.int_state[0] = 0; c.int_source[1] = 4;
        if (z80scc_device::valid_controller_recall(c)) {
            std::cerr << "FAIL: B02 SCC invalid IRQ source accepted\n"; return 30;
        }
    }
    // Bank conversion: exercise the imported native code, not only compile it.
    // An invalid input must fail without creating or overwriting the target.
    {
        eiii::bank_import::disc_info info{};
        std::string diag, summary;
        const fs::path bad = dir / "invalid-bank.iso";
        const fs::path target = dir / "bank-output.iso";
        write(bad, std::vector<char>(2048, char(0x23)));
        write(target, std::vector<char>(512, char(0x5a)));
        if (!eiii::bank_import::scan(bad.string(), info, diag) || info.emu3 ||
            eiii::bank_import::needs_conversion(bad.string()) ||
            eiii::bank_import::convert(bad.string(), target.string(), summary, diag) ||
            read(target) != std::vector<char>(512, char(0x5a))) {
            std::cerr << "FAIL: native bank converter invalid-source guard\\n";
            return 31;
        }
    }
    // The public conversion/mount interface must reject invalid input safely,
    // without firmware, changing media or creating a cached image.
    {
        eiii::runtime rt;
        std::string err;
        const fs::path bad = dir / "invalid-bank.iso";
        const fs::path cache = dir / "convert-cache";
        if (rt.mount_converted_bank_cd(bad.string(), cache.string(), err) ||
            err != "no active EIII CD-ROM drive at SCSI 3" || fs::exists(cache)) {
            std::cerr << "FAIL: invalid converted-bank mount guard\\n";
            return 32;
        }
    }
    // Synthetic EMU3 volume: exercise successful format recognition and
    // the no-bank conversion failure without shipping proprietary samples.
    {
        const fs::path image = dir / "empty-emu3.iso";
        const fs::path output = dir / "empty-converted.iso";
        std::vector<char> sectors(12 * 512, 0);
        sectors[0]='E'; sectors[1]='M'; sectors[2]='U'; sectors[3]='3';
        auto set32=[&](size_t offset,uint32_t v) {
            for (int i=0;i<4;++i) sectors[offset+i]=char((v>>(8*i))&255);
        };
        set32(8,7);   // volume label LBA
        set32(16,9);  // directory LBA
        set32(20,1);  // directory sectors
        set32(24,2);  // FAT LBA
        set32(28,1);  // FAT sectors
        set32(32,12); // data LBA
        sectors[0x28]=3; // cluster size encoding
        const char label[]="EMPTY EIII TEST";
        for (size_t i=0;i<sizeof(label)-1;++i) sectors[7*512+i]=label[i];
        write(image,sectors);
        eiii::bank_import::disc_info info{};
        std::string err,summary;
        write(output,std::vector<char>(512,char(0x5a)));
        if (!eiii::bank_import::scan(image.string(),info,err) ||
            !info.emu3 || info.eiii || info.eiiix || info.esi ||
            eiii::bank_import::needs_conversion(image.string()) ||
            eiii::bank_import::convert(image.string(),output.string(),summary,err) ||
            read(output)!=std::vector<char>(512,char(0x5a)) ||
            fs::exists(output.string()+".part")) {
            std::cerr << "FAIL: synthetic empty EMU3 volume import guard\\n";
            return 33;
        }
    }
    // Positive end-to-end conversion: construct a synthetic EMU3 volume
    // with one minimal EIII-format bank; verify the output can be rescanned.
    {
        const fs::path sourceBank=dir/"synthetic-bank.iso";
        const fs::path converted=dir/"synthetic-converted.iso";
        std::vector<char> disk(524*512,0);
        disk[0]='E';disk[1]='M';disk[2]='U';disk[3]='3';
        auto u32write=[&](size_t off,uint32_t n){
            for(unsigned j=0;j<4;++j)disk[off+j]=char(n>>(8*j));
        };
        u32write(8,7);u32write(16,9);u32write(20,1);
        u32write(24,2);u32write(28,1);u32write(32,12);
        disk[0x28]=3;
        const char name[]="SYNTH BANK      ";
        for(int j=0;j<16;++j)disk[9*512+j]=name[j];
        disk[9*512+0x12]=1; // FAT cluster number
        disk[9*512+0x1a]=char(0x81); // bank directory entry
        disk[2*512+2]=char(0xff);disk[2*512+3]=char(0x7f);
        const char magic[]="EMULATOR THREE ";
        for(int j=0;j<15;++j)disk[12*512+j]=magic[j];
        write(sourceBank,disk);
        // Force the Windows replacement path: destination already exists.
        write(converted,std::vector<char>(512,char(0x5a)));
        eiii::bank_import::disc_info before{},after{};
        std::string err,summary;
        if(!eiii::bank_import::scan(sourceBank.string(),before,err) ||
           !before.emu3 || before.eiii!=1 ||
           !eiii::bank_import::convert(sourceBank.string(),converted.string(),summary,err) ||
           !fs::exists(converted) || fs::exists(converted.string()+".previous") ||
           fs::exists(converted.string()+".part") ||
           !eiii::bank_import::scan(converted.string(),after,err) ||
           !after.emu3 || after.eiii!=1 || after.eiiix || after.esi ||
           summary.find("B00")==std::string::npos ||
           read(sourceBank)!=disk ||
           !eiii::runtime::check_cd_image(converted.string(),err) ||
           !eiii::bank_import::cache_fresh(sourceBank.string(),converted.string())) {
            std::cerr<<"FAIL: synthetic bank full conversion and rescan: "<<err<<"\\n";
            return 36;
        }
    }
    // Real conversion branches: synthetic EIIIX and ESI-32 records with
    // one playable preset/voice and a deterministic sample payload.
    for (int format=0;format<2;++format) {
        const fs::path src=dir/(format?"esi32-test.iso":"eiiix-test.iso");
        const fs::path dst=dir/(format?"esi32-result.iso":"eiiix-result.iso");
        std::vector<char> disk(524*512,0);
        auto put=[&](size_t at,uint32_t n) {
            for(int j=0;j<4;++j)disk[at+j]=char(n>>(8*j));
        };
        disk[0]='E';disk[1]='M';disk[2]='U';disk[3]='3';
        put(8,7);put(16,9);put(20,1);put(24,2);put(28,1);put(32,12);
        disk[0x28]=3;
        const char bankName[]="SYNTH TEST      ";
        for(int i=0;i<16;++i)disk[9*512+i]=bankName[i];
        disk[9*512+0x12]=1;disk[9*512+0x1A]=char(0x81);
        disk[2*512+2]=char(0xff);disk[2*512+3]=char(0x7f);
        const size_t base=12*512,ramSize=0x2d00,sndSize=512;
        const char* signature=format?"EMU SI-32 v3   ":"EMULATOR 3X    ";
        for(int i=0;i<15;++i)disk[base+i]=signature[i];
        put(base+0x30,uint32_t(ramSize));
        put(base+0x34,uint32_t(sndSize));
        const size_t ram=base+0x4C;
        put(ram+0x177e,0);put(ram+0x1782,0xbe); // X preset heap pointers
        const size_t preset=ram+0x2b26;
        for(int i=0;i<16;++i)disk[preset+i]=bankName[i];
        disk[preset + 0x8e + 1]=1; // first voice uses sample 1
        put(ram+0x1b82+4,0x400000); // sample pointer #1
        const size_t snd=ram+ramSize;
        for(int i=0;i<512;++i)disk[snd+i]=char((i*13+format)&255);
        write(src,disk);
        eiii::bank_import::disc_info before{},after{};
        std::string err,summary;
        if(!eiii::bank_import::scan(src.string(),before,err) ||
           (format?before.esi!=1:before.eiiix!=1) ||
           !eiii::bank_import::needs_conversion(src.string()) ||
           !eiii::bank_import::convert(src.string(),dst.string(),summary,err) ||
           !eiii::bank_import::scan(dst.string(),after,err) ||
           after.eiii!=1 || after.eiiix || after.esi ||
           !eiii::runtime::check_cd_image(dst.string(),err) ||
           summary.find("B00")==std::string::npos) {
            std::cerr<<"FAIL: synthetic "<<(format?"ESI-32":"EIIIX")
                     <<" bank conversion: "<<err<<" / "<<summary<<"\\n";
            return 37+format;
        }
        // Inspect the converted bank payload, not just its directory header.
        // Writer data starts at sector 111 (9 + 0x66), one 512-sector cluster.
        const auto bytes=read(dst);
        const size_t outputBank=111u*512u;
        auto get32=[&](size_t at)->uint32_t {
            if(at+4>bytes.size())return 0;
            uint32_t v=0;
            for(unsigned k=0;k<4;++k)v|=uint32_t(uint8_t(bytes[at+k]))<<(8*k);
            return v;
        };
        const size_t newRamSize=get32(outputBank+0x30);
        const size_t newSndSize=get32(outputBank+0x34);
        const size_t newRam=outputBank+0x4c;
        const size_t newSnd=newRam+newRamSize;
        const size_t firstVoice=newRam+(0x1a6fe - 0x1a000)+0x8e;
        const size_t newSamplePtr=newRam+(0x1a1b4-0x1a000)+4;
        bool identical=newSndSize==sndSize && newSnd+sndSize<=bytes.size();
        if(identical)for(size_t i=0;i<sndSize;++i)
            if(bytes[newSnd+i]!=disk[snd+i]) {identical=false;break;}
        if(!identical || firstVoice+2>=bytes.size() ||
           uint8_t(bytes[firstVoice+1])!=1 ||
           get32(newSamplePtr)!=0x400000u) {
            std::cerr<<"FAIL: "<<(format?"ESI-32":"EIIIX")
                     <<" sample bytes, voice mapping or sample pointer changed\\n";
            return 39+format;
        }
    }
    // The converter must never replace its own source image.
    {
        const fs::path same = dir / "same-image.iso";
        const std::vector<char> before(2048,char(0x42));
        write(same,before);
        std::string summary,err;
        if (eiii::bank_import::convert(same.string(),same.string(),summary,err) ||
            err.empty() || read(same)!=before ||
            fs::exists(same.string()+".part")) {
            std::cerr << "FAIL: bank converter self-overwrite guard\\n";
            return 35;
        }
    }
    // Conversion cache must not collide when two discs share a filename.
    {
        const fs::path ca = dir / "library-a" / "banks.iso";
        const fs::path cb = dir / "library-b" / "banks.iso";
        const fs::path cache = dir / "shared-cache";
        const auto a = eiii::bank_import::cached_path(ca.string(),cache.string());
        const auto b = eiii::bank_import::cached_path(cb.string(),cache.string());
        if (a == b || a != eiii::bank_import::cached_path(ca.string(),cache.string())) {
            std::cerr << "FAIL: bank conversion cache filename collision\\n";
            return 34;
        }
    }
    // B03 handshake integration: actual emulator worker, without proprietary ROM.
    // The main thread must receive a fresh acknowledged overlay snapshot,
    // across repeated activations, rather than relying on a 300ms poll delay.
    {
        eiii::engine engine;
        eiii::engine_config cfg;
        for (int cycle=0; cycle<5; ++cycle) {
            engine.start(cfg);
            for (int save=0; save<8; ++save) {
                if (!engine.refresh_hd_changes()) {
                    std::cerr << "FAIL: B03 save/engine snapshot handshake timed out\\n";
                    engine.shutdown();
                    return 8;
                }
            }
            engine.shutdown();
        }
    }
    std::cout << "PASS: missing-ROM, valid HD export, original protection, source fingerprint, LBA bounds\n";
    return 0;
}
