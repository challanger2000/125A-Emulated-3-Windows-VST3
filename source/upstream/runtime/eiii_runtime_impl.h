// Internal state of eiii::runtime (standalone diagnostics may use it; UI/plugin code must not include this header).
#pragma once
#include "eiii_runtime.h"
#include "progress_watchdog.h"
#include "../../windows/vst3-port/core_partial_snapshot.h"
#include "../../windows/vst3-port/core_execution_section.h"
#include "../../windows/vst3-port/core_checkpoint_bundle.h"
#include "../../windows/vst3-port/core_cpu_registers.h"
#include "../../windows/vst3-port/core_fpu_section.h"
#include "../../windows/vst3-port/core_scheduler_section.h"
#include "../../windows/vst3-port/core_ncr_section.h"
#include "../../windows/vst3-port/core_scsi_bus_section.h"
#include "../../windows/vst3-port/core_scsi_target_section.h"
#include "../../windows/vst3-port/core_hdd_section.h"
#include "../../windows/vst3-port/core_cd_section.h"
#include "../../windows/vst3-port/core_d0_section.h"
#include "../../windows/vst3-port/core_section_container.h"
#include "../../windows/vst3-port/core_section_roster.h"
#include "../../windows/vst3-port/core_scc_registers.h"
#include "../../windows/vst3-port/core_scc_controller.h"
#include "../../windows/vst3-port/core_scc_fifo.h"
#include "../../windows/vst3-port/core_scc_lines.h"
#include "../../windows/vst3-port/core_cpu_clock.h"
#include "../../windows/vst3-port/core_motherboard_lcd.h"
#include "../../windows/vst3-port/core_motherboard_panel.h"
#include "../../windows/vst3-port/core_motherboard_registers.h"
#include "../../windows/vst3-port/core_motherboard_pit.h"
#include "../../windows/vst3-port/core_fchip_section.h"
#include "../../windows/vst3-port/core_fir_section.h"
#include "../../windows/vst3-port/core_cem_v1_section.h"
#include "../../windows/vst3-port/core_cem_v2_section.h"
#include "../../windows/vst3-port/core_audio_timing.h"
#include "../../windows/vst3-port/core_audio_queue_section.h"
#include "../../windows/vst3-port/core_machine_context.h"
#include "../standalone/shim/image_fingerprint.h"
#include "eiii_machine.h"
#include "../../windows/vst3-port/core_serial_section.h"
#include "machine/nscsi_bus.h"
#include "imagedev/harddriv.h"
#include "imagedev/cdromimg.h"
#include "sound/cdda.h"
#include "../standalone/eiii_scsi.h"
#include <memory>
#include <cstdio>
#include <cmath>
#include <limits>
#include <utility>

namespace eiii {

struct runtime::impl
{
	// declared first: destroyed last (devices and timers unregister from it)
	shim_scheduler sched;
	runtime_config cfg;
	machine_t m;
	machine_config mcfg;
	std::unique_ptr<ns32016_device> cpu;
	std::unique_ptr<ns32081_device> fpu;
	std::unique_ptr<floppy_image_device> fdd;
	std::unique_ptr<wd1772_device> fdc;
	std::unique_ptr<nscsi_bus_device> scsi;
	std::unique_ptr<nscsi_harddisk_device> hd;
	std::unique_ptr<harddisk_image_device> hd_img;
	std::unique_ptr<nscsi_cdrom_device> cd;
	std::unique_ptr<cdrom_image_device> cd_img;
	std::unique_ptr<cdda_device> cd_cdda;
	// extra targets (IDs 4..7), present only when configured
	struct extra_target
	{
		std::unique_ptr<nscsi_harddisk_device> hd;
		std::unique_ptr<harddisk_image_device> hd_img;
		std::unique_ptr<nscsi_cdrom_device> cd;
		std::unique_ptr<cdrom_image_device> cd_img;
		std::unique_ptr<cdda_device> cdda;
	} extra[8];
	std::string warning;
	bool overlay_restore_failed=false;
	harddisk_image_device *hd_image(unsigned id) const { return id == 1 ? hd_img.get() : id < 8 ? extra[id].hd_img.get() : nullptr; }
	cdrom_image_device *cd_image(unsigned id) { return id == 3 ? cd_img.get() : id < 8 ? extra[id].cd_img.get() : nullptr; }
	std::unique_ptr<ncr5380_device> hdc;
	std::unique_ptr<scc85230_device> scc;
	std::unique_ptr<z80scc_channel> scc_cha, scc_chb;
	std::unique_ptr<software_backend> backend;

	// MIDI IN: bytes are clocked into SCC channel A RxD as 31250-baud 8N1 serial (idle high), like MAME's midiin port
	struct midi_tx_t { std::vector<std::pair<double, std::vector<u8>>> ev; size_t next = 0; std::vector<u8> q; size_t q_head = 0; int bit = -1; u8 cur = 0; } midi;
	std::unique_ptr<emu_timer> midi_timer, scan_irq_timer;

	int stop = 0;
	progress_watchdog watchdog;
	bool powered = false;
	std::vector<device_t *> order;

	void bind();
	bool open(runtime_config const &c, std::string &err);
	void power_on();
	void midi_tick();
	// B02 internal core capture/restore; owner-thread only, at a stopped
	// instruction boundary. Never call through VST3 process()/audio.
	struct core_recall_state {
		// Value-owned RAM, captured at the same stopped owner-thread boundary.
		std::vector<uint16_t> motherboard_ram;
		std::vector<uint16_t> sample_ram;
		machine_t::motherboard_recall_state motherboard;
		ns32016_device::recall_state cpu;
		cpu_device::clock_recall_state cpu_clock;
		ns32081_recall_state fpu;
		shim_scheduler::recall_state scheduler;
		nscsi_bus_device::recall_state scsi_bus;
		ncr5380_device::recall_state ncr;
		// Fixed SCSI ID roster: D1 HDD, D3 CD, optional D4..D7.
		std::array<bool,8> disk_present{};
		std::array<bool,8> cd_present{};
		bool d0_present=false;
		vst3::CoreD0Section::State d0{};
		// Nonserialized topology proof: the five post-legacy D0 timers must
		// occupy the exact tail before old scheduler arrays can be migrated.
		bool d0_timers_at_tail=false;
		bool migrated_legacy_d0_timers=false;
		std::array<nscsi_harddisk_device::disk_target_recall_state,8> disks{};
		std::array<nscsi_cdrom_device::cd_target_recall_state,8> cds{};
		software_backend::recall_state audio;
		midi_tx_t midi;
		vst3::CoreSerialSection::State serial;
		vst3::CoreMachineContext::State context;
		z80scc_device::controller_recall_state scc_controller;
		z80scc_channel::fifo_recall_state scc_a_fifo, scc_b_fifo;
		z80scc_channel::register_recall_state scc_a_regs, scc_b_regs;
		z80scc_channel::channel_line_recall_state scc_a_line, scc_b_line;
	};
	// Digital F-Chip voices only. Analog CEM, FIR and host-rate timing
	// remain outside this codec; never treat it as a complete audio recall.
	static bool encode_audio_queue(core_recall_state const& s,std::vector<uint8_t>& out){
		vst3::CoreAudioQueueSection::State w{};
		w.output=s.audio.output;w.cv=s.audio.cv;
		return vst3::CoreAudioQueueSection::encode(w,out);
	}
	static bool stage_audio_queue(uint8_t const* p,size_t n,core_recall_state& s){
		vst3::CoreAudioQueueSection::State w{};
		if(!vst3::CoreAudioQueueSection::decode(p,n,w))return false;
		s.audio.output=std::move(w.output);s.audio.cv=w.cv;
		return true;
	}
	static bool encode_audio_timing(core_recall_state const& s,std::vector<uint8_t>& out){
		vst3::CoreAudioTiming::State w{};
		w.values={s.audio.rate,s.audio.sample_rate,s.audio.next,s.audio.accr_l,s.audio.accr_r};
		w.output_frames=s.audio.output_frames;w.now=s.audio.now;w.sample_words=s.audio.sample_words;
		w.oversample=s.audio.oversample;w.phase=s.audio.decimation_phase;
		w.model_v2=s.audio.model_v2;
		return vst3::CoreAudioTiming::encode(w,out);
	}
	static bool stage_audio_timing(uint8_t const* p,size_t n,core_recall_state& s){
		vst3::CoreAudioTiming::State w{};
		if(!vst3::CoreAudioTiming::decode(p,n,w))return false;
		if(w.model_v2!=s.audio.model_v2||w.sample_words!=s.audio.sample_words||
		   w.oversample!=s.audio.oversample||w.values[0]!=s.audio.rate||
		   w.values[1]!=s.audio.sample_rate)return false;
		s.audio.output_frames=w.output_frames;s.audio.now=w.now;s.audio.next=w.values[2];
		s.audio.accr_l=w.values[3];s.audio.accr_r=w.values[4];
		s.audio.decimation_phase=w.phase;return true;
	}
	static bool encode_cem_v2(core_recall_state const& s,std::vector<uint8_t>& out){
		if(!s.audio.model_v2){
			std::vector<uint8_t> marker;vst3::CoreStateEnvelope::put32(marker,0x30563243u);
			return vst3::CoreStateEnvelope::encode(marker,out);
		}
		if(!s.audio.cem_v2)return false;
		vst3::CoreCemV2Section::State state{};
		if(!s.audio.cem_v2->capture_recall_state(state))return false;
		return vst3::CoreCemV2Section::encode(state,out);
	}
	static bool stage_cem_v2(uint8_t const* p,size_t n,core_recall_state& s){
		if(!s.audio.model_v2){
			std::vector<uint8_t> marker;
			return vst3::CoreStateEnvelope::decode(p,n,marker)&&marker.size()==4&&
			       vst3::CoreStateEnvelope::get32(marker.data())==0x30563243u;
		}
		if(!s.audio.cem_v2)return false;
		vst3::CoreCemV2Section::State state{};
		if(!vst3::CoreCemV2Section::decode(p,n,state))return false;
		return s.audio.cem_v2->restore_recall_state(state);
	}
	static bool encode_cem_v1(core_recall_state const& s,std::vector<uint8_t>& out){
		vst3::CoreCemV1Section::State state{};
		for(unsigned i=0;i<16;++i)state[i]=s.audio.cem_v1.voice(i).capture_recall_state();
		return vst3::CoreCemV1Section::encode(state,out);
	}
	static bool stage_cem_v1(uint8_t const* p,size_t n,core_recall_state& s){
		vst3::CoreCemV1Section::State state{};
		if(!vst3::CoreCemV1Section::decode(p,n,state))return false;
		auto candidate=s.audio.cem_v1;
		for(unsigned i=0;i<16;++i)
			if(!candidate.voice(i).restore_recall_state(state[i]))return false;
		s.audio.cem_v1=std::move(candidate);return true;
	}
	static bool encode_fir_recall(core_recall_state const& s,std::vector<uint8_t>& out){
		fir_decimator::recall_state state;
		if(!s.audio.model_v2) {
			std::vector<uint8_t> marker;
			vst3::CoreStateEnvelope::put32(marker,0x30524946u); // FIR0: unused in V1
			return vst3::CoreStateEnvelope::encode(marker,out);
		}
		if(!s.audio.decimator.capture_recall_state(state))return false;
		return vst3::CoreFirSection::encode(state,out);
	}
	static bool stage_fir_recall(uint8_t const* p,size_t n,core_recall_state& s){
		fir_decimator::recall_state state;
		if(!s.audio.model_v2) {
			std::vector<uint8_t> marker;
			return vst3::CoreStateEnvelope::decode(p,n,marker) &&
			       marker.size()==4 && vst3::CoreStateEnvelope::get32(marker.data())==0x30524946u;
		}
		if(!vst3::CoreFirSection::decode(p,n,state))return false;
		return s.audio.decimator.restore_recall_state(state);
	}
	static bool encode_fchip_voices(core_recall_state const& s,std::vector<uint8_t>& out){
		return vst3::CoreFchipSection::encode(s.audio.voices,out);
	}
	static bool stage_fchip_voices(uint8_t const* p,size_t n,core_recall_state& s){
		vst3::CoreFchipSection::State detached{};
		if(!vst3::CoreFchipSection::decode(p,n,detached))return false;
		s.audio.voices=std::move(detached);return true;
	}
	static bool encode_motherboard_pit(core_recall_state const& s,std::vector<uint8_t>& out){
		vst3::CoreMotherboardPit::State wire{};
		for(unsigned i=0;i<3;++i){
			auto const& a=s.motherboard.pit[i];auto& w=wire[i];
			w.clock_hz=a.clock_hz;w.last_t=a.last_t;w.mode=a.mode;w.rw=a.rw;w.bcd=a.bcd;
			w.reload=a.reload;w.count=a.count;w.load_delay=a.load_delay;w.latch=a.latch;
			w.flags={a.loaded,a.msb_next,a.gate,a.out,a.latched,a.out_known,a.latch_msb};
			w.rem=a.rem;
		}
		return vst3::CoreMotherboardPit::encode(wire,out);
	}
	static bool stage_motherboard_pit(uint8_t const* data,size_t n,core_recall_state& s){
		vst3::CoreMotherboardPit::State wire{};
		if(!vst3::CoreMotherboardPit::decode(data,n,wire))return false;
		for(unsigned i=0;i<3;++i){
			auto const& w=wire[i];auto& a=s.motherboard.pit[i];
			a.clock_hz=w.clock_hz;a.last_t=w.last_t;a.mode=w.mode;a.rw=w.rw;a.bcd=w.bcd;
			a.reload=w.reload;a.count=w.count;a.load_delay=w.load_delay;a.latch=w.latch;
			a.loaded=w.flags[0];a.msb_next=w.flags[1];a.gate=w.flags[2];
			a.out=w.flags[3];a.latched=w.flags[4];a.out_known=w.flags[5];
			a.latch_msb=w.flags[6];a.rem=w.rem;
		}
		return true;
	}
	static bool encode_motherboard_registers(core_recall_state const& s,std::vector<uint8_t>& out){
		vst3::CoreMotherboardRegisters::State w;
		auto const& v=s.motherboard;
		w.dig=v.dig_words;w.rf_latch=v.rf_latch;w.cv=v.cv_words;
		w.start=v.w_start;w.loop=v.w_loop;w.size=v.w_size;w.pending=v.w_pending;
		w.n_rf=v.n_rf;w.n_ctrl=v.n_ctrl;w.n_cv=v.n_cv;
		w.irq_latch=v.irq_latch;w.irq_state=v.irq_state;
		return vst3::CoreMotherboardRegisters::encode(w,out);
	}
	static bool stage_motherboard_registers(uint8_t const* p,size_t n,core_recall_state& s){
		vst3::CoreMotherboardRegisters::State w;
		if(!vst3::CoreMotherboardRegisters::decode(p,n,w))return false;
		auto& v=s.motherboard;
		v.dig_words=w.dig;v.rf_latch=w.rf_latch;v.cv_words=w.cv;
		v.w_start=w.start;v.w_loop=w.loop;v.w_size=w.size;v.w_pending=w.pending;
		v.n_rf=w.n_rf;v.n_ctrl=w.n_ctrl;v.n_cv=w.n_cv;
		v.irq_latch=w.irq_latch;v.irq_state=w.irq_state;return true;
	}
	static bool encode_motherboard_panel(core_recall_state const& s,std::vector<uint8_t>& out){
		vst3::CoreMotherboardPanel::State w;auto const& a=s.motherboard;
		w.leds=a.leds;w.misc=a.misc;w.fdc_status=a.fdc_status;
		w.extra_switches=a.extra_switches;w.adc_ctrl=a.adc_ctrl;
		w.scan_fifo=a.scan_fifo;w.scan_head=a.scan_head;
		w.scan_tail=a.scan_tail;w.scan_count=a.scan_count;
		w.adc_latch=a.adc_latch;w.adc_pot=a.adc_pot;
		return vst3::CoreMotherboardPanel::encode(w,out);
	}
	static bool stage_motherboard_panel(uint8_t const* data,size_t n,core_recall_state& s){
		vst3::CoreMotherboardPanel::State w;
		if(!vst3::CoreMotherboardPanel::decode(data,n,w))return false;
		auto& a=s.motherboard;
		a.leds=w.leds;a.misc=w.misc;a.fdc_status=w.fdc_status;
		a.extra_switches=w.extra_switches;a.adc_ctrl=w.adc_ctrl;
		a.scan_fifo=w.scan_fifo;a.scan_head=w.scan_head;
		a.scan_tail=w.scan_tail;a.scan_count=w.scan_count;
		a.adc_latch=w.adc_latch;a.adc_pot=w.adc_pot;return true;
	}
	static bool encode_motherboard_lcd(core_recall_state const& s,std::vector<uint8_t>& out){
		vst3::CoreMotherboardLcd::State w;
		auto const& x=s.motherboard;
		w.ddram=x.lcd_ddram;w.cgram=x.lcd_cgram;
		w.ac=x.lcd_ac;w.dir=x.lcd_dir;w.shift=x.lcd_disp_shift;w.lines=x.lcd_num_line;
		w.cgram_active=x.lcd_cgram_active;w.shift_on=x.lcd_shift_on;
		w.display_on=x.lcd_display_on;w.cursor_on=x.lcd_cursor_on;
		w.blink_on=x.lcd_blink_on;
		std::memcpy(&w.busy_until_bits,&x.lcd_busy_until,8);
		return vst3::CoreMotherboardLcd::encode(w,out);
	}
	static bool stage_motherboard_lcd(uint8_t const* p,size_t n,core_recall_state& s){
		vst3::CoreMotherboardLcd::State w;
		if(!vst3::CoreMotherboardLcd::decode(p,n,w))return false;
		double busy=0;std::memcpy(&busy,&w.busy_until_bits,8);
		if(!std::isfinite(busy)||busy<0)return false;
		auto& x=s.motherboard;
		x.lcd_ddram=w.ddram;x.lcd_cgram=w.cgram;
		x.lcd_ac=w.ac;x.lcd_dir=w.dir;x.lcd_disp_shift=w.shift;x.lcd_num_line=w.lines;
		x.lcd_cgram_active=w.cgram_active;x.lcd_shift_on=w.shift_on;
		x.lcd_display_on=w.display_on;x.lcd_cursor_on=w.cursor_on;
		x.lcd_blink_on=w.blink_on;x.lcd_busy_until=busy;
		return true;
	}
	static bool encode_cpu_clock(core_recall_state const& s,std::vector<uint8_t>& out) {
		vst3::CoreCpuClock::State w;
		w.completed_cycles=s.cpu_clock.completed_cycles;
		w.debugger_pc=s.cpu_clock.debugger_pc;
		return vst3::CoreCpuClock::encode(w,out);
	}
	static bool stage_cpu_clock(uint8_t const* data,size_t n,core_recall_state& s) {
		vst3::CoreCpuClock::State w;
		if(!vst3::CoreCpuClock::decode(data,n,w))return false;
		s.cpu_clock.completed_cycles=w.completed_cycles;
		s.cpu_clock.debugger_pc=w.debugger_pc;
		return true;
	}
	static bool encode_scc_lines(core_recall_state const& s,std::vector<uint8_t>& out){
		vst3::CoreSccLines::State w;
		auto fill=[](auto const& a,auto& b){
			b.brg_counter=a.brg_counter;b.brg_rate=a.brg_rate;
			b.delayed_tx_brg_change=a.delayed_tx_brg_change;
			b.rx_error=a.rx_error;b.tx_error=a.tx_error;
			b.extint_latch=a.extint_latch;b.extint_states=a.extint_states;
			b.lines={a.rx_clock,a.rx_first,a.rx_break,a.rxd,a.ri,
			         a.tx_clock,a.tx_int_disarm,a.dtr,a.rts,a.rcv_mode,a.ph,a.rxc,a.txc};
		};
		fill(s.scc_a_line,w.a);fill(s.scc_b_line,w.b);
		return vst3::CoreSccLines::encode(w,out);
	}
	static bool stage_scc_lines(uint8_t const* p,size_t n,core_recall_state& s){
		vst3::CoreSccLines::State w;
		if(!vst3::CoreSccLines::decode(p,n,w))return false;
		auto fill=[](auto const& a,auto& b){
			b.brg_counter=a.brg_counter;b.brg_rate=a.brg_rate;
			b.delayed_tx_brg_change=a.delayed_tx_brg_change;
			b.rx_error=a.rx_error;b.tx_error=a.tx_error;
			b.extint_latch=a.extint_latch;b.extint_states=a.extint_states;
			b.rx_clock=a.lines[0];b.rx_first=a.lines[1];b.rx_break=a.lines[2];
			b.rxd=a.lines[3];b.ri=a.lines[4];b.tx_clock=a.lines[5];
			b.tx_int_disarm=a.lines[6];b.dtr=a.lines[7];b.rts=a.lines[8];
			b.rcv_mode=a.lines[9];b.ph=a.lines[10];b.rxc=a.lines[11];b.txc=a.lines[12];
		};
		fill(w.a,s.scc_a_line);fill(w.b,s.scc_b_line);return true;
	}
	static bool encode_scc_fifo(core_recall_state const& s,std::vector<uint8_t>& out){
		vst3::CoreSccFifo::State w;
		auto copy=[&](auto const& from,auto& to){
			to.cursors={from.cursors.rx_read,from.cursors.rx_write,from.cursors.rx_capacity,
			            from.cursors.tx_read,from.cursors.tx_write,from.cursors.tx_capacity};
			for(int i=0;i<9;++i){to.rx_data[i]=from.rx_data[i];to.rx_errors[i]=from.rx_errors[i];}
			for(int i=0;i<4;++i){to.tx_data[i]=from.tx_data[i];to.tx_errors[i]=from.tx_errors[i];}
		};
		copy(s.scc_a_fifo,w.a);copy(s.scc_b_fifo,w.b);
		return vst3::CoreSccFifo::encode(w,out);
	}
	static bool stage_scc_fifo(uint8_t const* data,size_t n,core_recall_state& s){
		vst3::CoreSccFifo::State w;
		if(!vst3::CoreSccFifo::decode(data,n,w))return false;
		auto copy=[&](auto const& from,auto& to){
			to.cursors={from.cursors[0],from.cursors[1],from.cursors[2],
			            from.cursors[3],from.cursors[4],from.cursors[5]};
			for(int i=0;i<9;++i){to.rx_data[i]=from.rx_data[i];to.rx_errors[i]=from.rx_errors[i];}
			for(int i=0;i<4;++i){to.tx_data[i]=from.tx_data[i];to.tx_errors[i]=from.tx_errors[i];}
		};
		copy(w.a,s.scc_a_fifo);copy(w.b,s.scc_b_fifo);return true;
	}
	static bool encode_scc_controller(core_recall_state const& s,std::vector<uint8_t>& out){
		vst3::CoreSccController::State w;
		w.wr9=s.scc_controller.wr9;w.wr0_ptrbits=s.scc_controller.wr0_ptrbits;
		w.out_int_state=s.scc_controller.out_int_state;
		for(unsigned i=0;i<6;++i){
			w.int_state[i]=s.scc_controller.int_state[i];
			w.int_source[i]=s.scc_controller.int_source[i];
		}
		return vst3::CoreSccController::encode(w,out);
	}
	static bool stage_scc_controller(uint8_t const* p,size_t n,core_recall_state& s){
		vst3::CoreSccController::State w;
		if(!vst3::CoreSccController::decode(p,n,w))return false;
		s.scc_controller.wr9=w.wr9;s.scc_controller.wr0_ptrbits=w.wr0_ptrbits;
		s.scc_controller.out_int_state=w.out_int_state;
		for(unsigned i=0;i<6;++i){
			s.scc_controller.int_state[i]=w.int_state[i];
			s.scc_controller.int_source[i]=w.int_source[i];
		}
		return true;
	}
	static bool encode_scc_registers(core_recall_state const& state,
	                                 std::vector<uint8_t>& out) {
		vst3::CoreSccRegisters::State w;
		for(unsigned i=0;i<16;++i) {
			w.a.rr[i]=state.scc_a_regs.rr[i];w.a.wr[i]=state.scc_a_regs.wr[i];
			w.b.rr[i]=state.scc_b_regs.rr[i];w.b.wr[i]=state.scc_b_regs.wr[i];
		}
		w.a.wr7p=state.scc_a_regs.wr7p;w.b.wr7p=state.scc_b_regs.wr7p;
		return vst3::CoreSccRegisters::encode(w,out);
	}
	static bool stage_scc_registers(uint8_t const* data,size_t size,
	                                core_recall_state& state) {
		vst3::CoreSccRegisters::State w;
		if(!vst3::CoreSccRegisters::decode(data,size,w))return false;
		for(unsigned i=0;i<16;++i) {
			state.scc_a_regs.rr[i]=w.a.rr[i];state.scc_a_regs.wr[i]=w.a.wr[i];
			state.scc_b_regs.rr[i]=w.b.rr[i];state.scc_b_regs.wr[i]=w.b.wr[i];
		}
		state.scc_a_regs.wr7p=w.a.wr7p;state.scc_b_regs.wr7p=w.b.wr7p;
		return true;
	}
	// Timer indexes refer to the existing device topology; no timer pointers
	// or callbacks are serialized. Staging never mutates the live scheduler.
	// Canonically pack validated component streams from the same owner-thread
	// snapshot. Omitted devices remain a hard release gate: not yet full B02.
	static bool encode_core_device_sections(core_recall_state const &state,
	                                        std::vector<uint8_t> &out) {
		if(!software_backend::valid_recall_state(state.audio))return false;
		std::vector<vst3::CoreSectionContainer::Section> sections;
		auto append=[&](uint32_t id, auto encoder)->bool {
			std::vector<uint8_t> bytes;
			if(!encoder(bytes)) { fprintf(stderr, "B02 encode section %u failed\n", id); return false; }
			sections.push_back({id,std::move(bytes)});
			return true;
		};
		if(!append(1,[&](auto& v){return encode_checkpoint_bundle(state,v);}) ||
		   !append(2,[&](auto& v){return encode_cpu_registers(state,v);}) ||
		   !append(3,[&](auto& v){return encode_fpu_recall(state,v);}) ||
		   !append(4,[&](auto& v){return encode_scheduler_recall(state,v);}) ||
		   !append(5,[&](auto& v){return encode_ncr_recall(state,v);}) ||
		   !append(6,[&](auto& v){return encode_scsi_bus_recall(state,v);}) ||
		   !append(7,[&](auto& v){return encode_scc_controller(state,v);}) ||
		   !append(8,[&](auto& v){return encode_scc_registers(state,v);}) ||
		   !append(9,[&](auto& v){return encode_scc_fifo(state,v);}) ||
		   !append(10,[&](auto& v){return encode_scc_lines(state,v);}) ||
		   !append(11,[&](auto& v){return encode_cpu_clock(state,v);}) ||
		   !append(12,[&](auto& v){return encode_motherboard_lcd(state,v);}) ||
		   !append(13,[&](auto& v){return encode_motherboard_panel(state,v);}) ||
		   !append(14,[&](auto& v){return encode_motherboard_registers(state,v);}) ||
		   !append(15,[&](auto& v){return encode_motherboard_pit(state,v);}) ||
		   !append(16,[&](auto& v){return encode_fchip_voices(state,v);}) ||
		   !append(17,[&](auto& v){return encode_fir_recall(state,v);}) ||
		   !append(18,[&](auto& v){return encode_cem_v1(state,v);}) ||
		   !append(19,[&](auto& v){return encode_cem_v2(state,v);}) ||
		   !append(20,[&](auto& v){return encode_audio_timing(state,v);}) ||
		   !append(21,[&](auto& v){return encode_audio_queue(state,v);}))
			return false;
		if(!append(22,[&](auto& v){return vst3::CoreSerialSection::encode(state.serial,v);}))return false;
		if(!append(23,[&](auto& v){return vst3::CoreMachineContext::encode(state.context,v);}))return false;
		for(unsigned id=0;id<8;++id) {
			if(state.disk_present[id] &&
			   (!append(30+id*3,[&](auto& v){return encode_scsi_target(state.disks[id].scsi,v);}) ||
			    !append(31+id*3,[&](auto& v){return encode_hdd_specific(state.disks[id],v);})))
				return false;
			if(state.cd_present[id] &&
			   (!append(30+id*3,[&](auto& v){return encode_scsi_target(state.cds[id].scsi,v);}) ||
			    !append(32+id*3,[&](auto& v){return encode_cd_specific(state.cds[id],v);})))
				return false;
		}
		if(state.d0_present && !append(60,[&](auto& v){return vst3::CoreD0Section::encode(state.d0,v);}))return false;
		if(!vst3::CoreSectionRoster::valid(sections,state.disk_present,state.cd_present,state.d0_present))return false;
		return vst3::CoreSectionContainer::encode(sections,out);
	}
	// Decode only into a detached candidate; never touch live devices here.
	static bool stage_core_device_sections(uint8_t const *data,size_t size,
	                                      core_recall_state const &base,
	                                      core_recall_state &out) {
		std::vector<vst3::CoreSectionContainer::Section> sections;
		if(!vst3::CoreSectionContainer::decode(data,size,sections) ||
		   !vst3::CoreSectionRoster::valid(sections,base.disk_present,base.cd_present,base.d0_present))
			return false;
		auto candidate=std::make_unique<core_recall_state>(base);
		auto apply=[&](size_t index,auto decoder) {
			bool ok=decoder(sections[index].bytes.data(),sections[index].bytes.size());
			if(!ok)fprintf(stderr,"B02 decode section %u rejected\n",sections[index].id);
			return ok;
		};
		if(!apply(0,[&](auto p,auto n){return stage_checkpoint_bundle(p,n,base,*candidate);}))return false;
		auto checkpoint_pc=candidate->cpu.pc;
		auto checkpoint_cycles=candidate->cpu_clock.completed_cycles;
		if(!apply(1,[&](auto p,auto n){return stage_cpu_registers(p,n,*candidate);}) ||
		   !apply(2,[&](auto p,auto n){return stage_fpu_recall(p,n,*candidate);}) ||
		   !apply(3,[&](auto p,auto n){return stage_scheduler_recall(p,n,*candidate);}) ||
		   !apply(4,[&](auto p,auto n){return stage_ncr_recall(p,n,*candidate);}) ||
		   !apply(5,[&](auto p,auto n){return stage_scsi_bus_recall(p,n,*candidate);}) ||
		   !apply(6,[&](auto p,auto n){return stage_scc_controller(p,n,*candidate);}) ||
		   !apply(7,[&](auto p,auto n){return stage_scc_registers(p,n,*candidate);}) ||
		   !apply(8,[&](auto p,auto n){return stage_scc_fifo(p,n,*candidate);}) ||
		   !apply(9,[&](auto p,auto n){return stage_scc_lines(p,n,*candidate);}) ||
		   !apply(10,[&](auto p,auto n){return stage_cpu_clock(p,n,*candidate);}) ||
		   !apply(11,[&](auto p,auto n){return stage_motherboard_lcd(p,n,*candidate);}) ||
		   !apply(12,[&](auto p,auto n){return stage_motherboard_panel(p,n,*candidate);}) ||
		   !apply(13,[&](auto p,auto n){return stage_motherboard_registers(p,n,*candidate);}) ||
		   !apply(14,[&](auto p,auto n){return stage_motherboard_pit(p,n,*candidate);}) ||
		   !apply(15,[&](auto p,auto n){return stage_fchip_voices(p,n,*candidate);}) ||
		   !apply(16,[&](auto p,auto n){return stage_fir_recall(p,n,*candidate);}) ||
		   !apply(17,[&](auto p,auto n){return stage_cem_v1(p,n,*candidate);}) ||
		   !apply(18,[&](auto p,auto n){return stage_cem_v2(p,n,*candidate);}) ||
		   !apply(19,[&](auto p,auto n){return stage_audio_timing(p,n,*candidate);}) ||
		   !apply(20,[&](auto p,auto n){return stage_audio_queue(p,n,*candidate);}))
			return false;
		if(candidate->cpu.pc!=checkpoint_pc || candidate->cpu_clock.completed_cycles!=checkpoint_cycles)return false;
		if(!apply(21,[&](auto p,auto n){return vst3::CoreSerialSection::decode(p,n,candidate->serial);}))return false;
		if(!apply(22,[&](auto p,auto n){return vst3::CoreMachineContext::decode(p,n,candidate->context);}))return false;
		size_t index=23;
		for(unsigned id=0;id<8;++id) {
			if(!base.disk_present[id]&&!base.cd_present[id])continue;
			bool disk=base.disk_present[id];
			auto& target=disk?candidate->disks[id].scsi:candidate->cds[id].scsi;
			if(!decode_scsi_target(sections[index].bytes.data(),sections[index].bytes.size(),target))
				return false;
			++index;
			bool valid=disk
				?stage_hdd_specific(sections[index].bytes.data(),sections[index].bytes.size(),candidate->disks[id])
				:stage_cd_specific(sections[index].bytes.data(),sections[index].bytes.size(),candidate->cds[id]);
			if(!valid)return false;
			++index;
		}
		if(base.d0_present) {
			if(index>=sections.size() ||
			   !vst3::CoreD0Section::decode(sections[index].bytes.data(),
			      sections[index].bytes.size(),candidate->d0))return false;
			++index;
		}
		if(index!=sections.size())return false;
		if(candidate->context.rom!=base.context.rom || candidate->context.overlays!=base.context.overlays) {
			fprintf(stderr,"B02 ROM or disk-overlay identity mismatch\n");return false;
		}
		// Reject inconsistent audio sections before publishing detached state.
		if(!software_backend::valid_recall_state(candidate->audio))return false;
		out=std::move(*candidate);
		return true;
	}
	static bool encode_cd_specific(nscsi_cdrom_device::cd_target_recall_state const &s,
	                               std::vector<uint8_t> &out) {
		vst3::CoreCdSection::State w;
		w.sector=s.sector;w.mode=s.mode;w.transfer=s.transfer;
		w.media_length=s.media_length;w.media_fingerprint=s.media_fingerprint;
		w.media_present=s.media_present;w.media_sequence=s.media_sequence;w.cached_media_sequence=s.cached_media_sequence;
		w.lba=s.lba;w.cached_sector=s.cached_sector;w.block_size=s.block_size;
		w.mode_size=s.mode_size;w.sotc=s.sotc;
		w.removal_prevented=s.removal_prevented;
		w.transfer_position=s.transfer_position;
		return vst3::CoreCdSection::encode(w,out);
	}
	static bool stage_cd_specific(uint8_t const *data,size_t size,
	                              nscsi_cdrom_device::cd_target_recall_state &out) {
		vst3::CoreCdSection::State w;
		if(!vst3::CoreCdSection::decode(data,size,w))return false;
		auto staged=out;
		staged.sector=w.sector;staged.mode=w.mode;
		staged.transfer=std::move(w.transfer);
		staged.media_length=w.media_length;
		staged.media_fingerprint=w.media_fingerprint;
		staged.media_present=w.media_present;
		staged.media_sequence=w.media_sequence;staged.cached_media_sequence=w.cached_media_sequence;
		staged.lba=w.lba;staged.cached_sector=w.cached_sector;
		staged.block_size=w.block_size;staged.mode_size=w.mode_size;
		staged.sotc=w.sotc;staged.removal_prevented=w.removal_prevented;
		staged.transfer_position=w.transfer_position;
		out=std::move(staged);return true;
	}
	static bool encode_hdd_specific(nscsi_harddisk_device::disk_target_recall_state const &s,
	                                std::vector<uint8_t> &out) {
		vst3::CoreHddSection::State w;
		w.sector=s.sector;w.lba=s.lba;w.cur_lba=s.cur_lba;
		w.remaining_blocks=s.remaining_blocks;w.bytes_per_sector=s.bytes_per_sector;
		w.inquiry=s.inquiry;w.model_name=s.model_name;
		w.media_present=s.media_present;w.media_length=s.media_length;
		w.media_fingerprint=s.media_fingerprint;w.seek_model=s.seek_model;
		w.last_cylinder=s.last_cylinder;w.interleave=s.interleave;
		w.rpm=s.rpm;w.seek_track_us=s.seek_track_us;
		w.seek_range_us=s.seek_range_us;w.seek_exponent=s.seek_exponent;
		return vst3::CoreHddSection::encode(w,out);
	}
	static bool stage_hdd_specific(uint8_t const *data,size_t size,
	                               nscsi_harddisk_device::disk_target_recall_state &out) {
		vst3::CoreHddSection::State w;
		if(!vst3::CoreHddSection::decode(data,size,w))return false;
		auto s=out;
		s.sector=w.sector;s.lba=w.lba;s.cur_lba=w.cur_lba;
		s.remaining_blocks=w.remaining_blocks;s.bytes_per_sector=w.bytes_per_sector;
		s.inquiry=std::move(w.inquiry);s.model_name=std::move(w.model_name);
		s.media_present=w.media_present;s.media_length=w.media_length;
		s.media_fingerprint=w.media_fingerprint;s.seek_model=w.seek_model;
		s.last_cylinder=w.last_cylinder;s.interleave=w.interleave;
		s.rpm=w.rpm;s.seek_track_us=w.seek_track_us;
		s.seek_range_us=w.seek_range_us;s.seek_exponent=w.seek_exponent;
		out=std::move(s);return true;
	}
	// Common command/queue execution state of both HDD and CD targets.
	static bool encode_scsi_target(nscsi_full_device::target_recall_state const &s,
	                               std::vector<uint8_t> &out) {
		vst3::CoreScsiTargetSection::State w;
		w.command=s.command;w.sense=s.sense;
		w.command_size=s.command_size;w.identify=s.identify;
		w.state=s.state;w.substate=s.substate;w.initiator_id=s.initiator_id;
		w.buffer_id=s.buffer_id;w.buffer_size=s.buffer_size;w.buffer_pos=s.buffer_pos;
		for(size_t i=0;i<32;++i)
			w.actions[i]={s.actions[i].action,s.actions[i].param1,s.actions[i].param2};
		w.queue_read=s.queue_read;w.queue_write=s.queue_write;
		return vst3::CoreScsiTargetSection::encode(w,out);
	}
	static bool decode_scsi_target(uint8_t const* data,size_t size,
	                               nscsi_full_device::target_recall_state &out) {
		vst3::CoreScsiTargetSection::State w;
		if(!vst3::CoreScsiTargetSection::decode(data,size,w))return false;
		nscsi_full_device::target_recall_state s;
		s.command=w.command;s.sense=w.sense;
		s.command_size=w.command_size;s.identify=w.identify;
		s.state=w.state;s.substate=w.substate;s.initiator_id=w.initiator_id;
		s.buffer_id=w.buffer_id;s.buffer_size=w.buffer_size;s.buffer_pos=w.buffer_pos;
		for(size_t i=0;i<32;++i){
			s.actions[i].action=w.actions[i].action;
			s.actions[i].param1=w.actions[i].param1;
			s.actions[i].param2=w.actions[i].param2;
		}
		s.queue_read=w.queue_read;s.queue_write=w.queue_write;
		if(!nscsi_full_device::valid_target_recall(s))return false;
		out=std::move(s);return true;
	}
	static bool encode_scsi_bus_recall(core_recall_state const &state,
	                                   std::vector<uint8_t> &out) {
		vst3::CoreScsiBusSection::State wire;
		auto const &bus=state.scsi_bus;
		if(bus.device_count<0||bus.device_count>16)return false;
		wire.ctrl=bus.ctrl;wire.data=bus.data;
		for(int i=0;i<bus.device_count;++i){
			auto const &d=bus.devices[size_t(i)];
			wire.devices.push_back({d.tag,d.ctrl,d.wait_ctrl,d.data});
		}
		return vst3::CoreScsiBusSection::encode(wire,out);
	}
	static bool stage_scsi_bus_recall(uint8_t const *data,size_t size,
	                                  core_recall_state &candidate) {
		vst3::CoreScsiBusSection::State wire;
		if(!vst3::CoreScsiBusSection::decode(data,size,wire))return false;
		nscsi_bus_device::recall_state staged;
		staged.device_count=int(wire.devices.size());
		staged.ctrl=wire.ctrl;staged.data=wire.data;
		for(size_t i=0;i<wire.devices.size();++i){
			auto const &d=wire.devices[i];
			staged.devices[i]={d.tag,d.ctrl,d.wait_ctrl,d.data};
		}
		candidate.scsi_bus=std::move(staged);
		return true;
	}
	// NCR5380: preserve register contents and line levels, without callbacks.
	static bool encode_ncr_recall(core_recall_state const &state,
	                              std::vector<uint8_t> &out) {
		vst3::CoreNcrSection::State wire;
		auto const &n=state.ncr;
		wire.phase=n.phase;wire.ctrl=n.scsi_ctrl;
		wire.regs[0]=n.odata;wire.regs[1]=n.icmd;
		wire.regs[2]=n.mode;wire.regs[3]=n.tcmd;
		wire.regs[4]=n.bas;wire.regs[5]=n.idata;wire.regs[6]=n.selen;
		wire.flags=uint8_t(n.irq_state)|uint8_t(n.drq_state)<<1|
		           uint8_t(n.rst_out)<<2|uint8_t(n.rst_state)<<3;
		return vst3::CoreNcrSection::encode(wire,out);
	}
	static bool stage_ncr_recall(uint8_t const *data,size_t size,
	                             core_recall_state &candidate) {
		vst3::CoreNcrSection::State wire;
		if(!vst3::CoreNcrSection::decode(data,size,wire))return false;
		auto &n=candidate.ncr;
		n.phase=wire.phase;n.scsi_ctrl=wire.ctrl;
		n.odata=wire.regs[0];n.icmd=wire.regs[1];n.mode=wire.regs[2];
		n.tcmd=wire.regs[3];n.bas=wire.regs[4];n.idata=wire.regs[5];
		n.selen=wire.regs[6];
		n.irq_state=bool(wire.flags&1);n.drq_state=bool(wire.flags&2);
		n.rst_out=bool(wire.flags&4);n.rst_state=bool(wire.flags&8);
		return true;
	}
	static bool encode_scheduler_recall(core_recall_state const &state,
	                                    std::vector<uint8_t> &out) {
		vst3::CoreSchedulerSection::State wire;
		wire.basetime=state.scheduler.basetime.m_seconds;
		for(auto const &t:state.scheduler.timers)
			wire.timers.push_back({t.expire.m_seconds,t.start.m_seconds,
			                       t.period.m_seconds,t.param,t.enabled});
		return vst3::CoreSchedulerSection::encode(wire,out);
	}
	static bool stage_scheduler_recall(uint8_t const *data,size_t size,
	                                   core_recall_state &candidate) {
		vst3::CoreSchedulerSection::State wire;
		if(!vst3::CoreSchedulerSection::decode(data,size,wire))return false;
		shim_scheduler::recall_state staging;
		staging.basetime=attotime(wire.basetime);
		for(auto const &t:wire.timers)
			staging.timers.push_back({attotime(t.expire),
			    attotime(t.start),attotime(t.period),
			    t.param,t.enabled});
		// Detached codec tests have no device topology and therefore no
		// live timer roster to compare. Real restore candidates always do.
		if(!candidate.scheduler.timers.empty() &&
		   staging.timers.size()!=candidate.scheduler.timers.size()) {
			if(candidate.d0_present || !candidate.d0_timers_at_tail ||
			   staging.timers.size()+5!=candidate.scheduler.timers.size())return false;
			// Pre-D0 full-machine states contain five fewer MAME timers.
			// The new drive/controller has no media in that state: give its
			// five new timers a disabled, future-safe state at the restored time.
			for(unsigned i=0;i<5;++i)
				staging.timers.push_back({attotime::never,staging.basetime,
				   attotime::never,0,false});
			candidate.migrated_legacy_d0_timers=true;
		}
		candidate.scheduler=std::move(staging);
		return true;
	}
	static bool encode_fpu_recall(core_recall_state const &state,
	                              std::vector<uint8_t> &out) {
		vst3::CoreFpuSection::State wire;
		auto const &f=state.fpu;
		wire.fsr=f.fsr;wire.state=f.state;wire.tcy=f.tcy;
		wire.idbyte=f.idbyte;wire.opword=f.opword;wire.status=f.status;
		for(unsigned i=0;i<3;++i){
			wire.op[i].expected=f.op[i].expected;
			wire.op[i].issued=f.op[i].issued;
			wire.op[i].value=f.op[i].value;
		}
		for(unsigned i=0;i<8;++i)wire.f[i]=f.f[i];
		return vst3::CoreFpuSection::encode(wire,out);
	}
	static bool stage_fpu_recall(uint8_t const *data,size_t size,
	                             core_recall_state &candidate) {
		vst3::CoreFpuSection::State wire;
		if(!vst3::CoreFpuSection::decode(data,size,wire))return false;
		auto &f=candidate.fpu;
		f.fsr=wire.fsr;f.state=wire.state;f.tcy=wire.tcy;
		f.idbyte=wire.idbyte;f.opword=wire.opword;f.status=wire.status;
		for(unsigned i=0;i<3;++i){
			f.op[i].expected=wire.op[i].expected;
			f.op[i].issued=wire.op[i].issued;
			f.op[i].value=wire.op[i].value;
		}
		for(unsigned i=0;i<8;++i)f.f[i]=wire.f[i];
		return true;
	}
	static bool encode_cpu_registers(core_recall_state const &state,
	                                 std::vector<uint8_t> &out) {
		vst3::CoreCpuRegisters::State wire;
		auto const &cpu=state.cpu;
		wire.prefix={{cpu.ssp,cpu.pc,cpu.sb,cpu.fp,cpu.sp1,cpu.sp0,cpu.intbase,cpu.cfg,0}};
		for(unsigned i=0;i<8;++i)wire.r[i]=cpu.r[i];
		wire.sps=cpu.sps;wire.psr=cpu.psr;wire.mod=cpu.mod;
		wire.lines=uint8_t(cpu.nmi_line)|uint8_t(cpu.int_line)<<1|
			uint8_t(cpu.wait)<<2|uint8_t(cpu.sequential)<<3|uint8_t(cpu.ready)<<4;
		return vst3::CoreCpuRegisters::encode(wire,out);
	}
	static bool stage_cpu_registers(uint8_t const *data,size_t size,
	                                core_recall_state &candidate) {
		vst3::CoreCpuRegisters::State wire;
		if(!vst3::CoreCpuRegisters::decode(data,size,wire))return false;
		auto &cpu=candidate.cpu;
		cpu.ssp=wire.prefix[0];cpu.pc=wire.prefix[1];cpu.sb=wire.prefix[2];
		cpu.fp=wire.prefix[3];cpu.sp1=wire.prefix[4];cpu.sp0=wire.prefix[5];
		cpu.intbase=wire.prefix[6];cpu.cfg=wire.prefix[7];
		for(unsigned i=0;i<8;++i)cpu.r[i]=wire.r[i];
		cpu.sps=wire.sps;cpu.psr=wire.psr;cpu.mod=wire.mod;
		cpu.nmi_line=bool(wire.lines&1);cpu.int_line=bool(wire.lines&2);
		cpu.wait=bool(wire.lines&4);cpu.sequential=bool(wire.lines&8);
		cpu.ready=bool(wire.lines&16);
		return true;
	}
	// One wire bundle for captured RAM, MIDI and CPU execution position.
	// This remains partial: do not feed it to the full core restore API.
	static bool encode_checkpoint_bundle(core_recall_state const &state,
	                                     std::vector<uint8_t> &out) {
		vst3::CoreMidiSection::State midi_state;
		if (!capture_partial_transport(state, midi_state)) return false;
		vst3::CoreExecutionSection::State execution;
		execution.pc = state.cpu.pc;
		execution.completed_cycles = state.cpu_clock.completed_cycles;
		return vst3::CoreCheckpointBundle::encode(
			state.motherboard_ram.data(), state.motherboard_ram.size(),
			state.sample_ram.data(), state.sample_ram.size(),
			midi_state, execution, out);
	}
	// Reconstruct only known wire fields in a detached full-state candidate.
	// The untouched fields must originate from a separately validated snapshot.
	static bool stage_checkpoint_bundle(uint8_t const *data, size_t size,
	                                    core_recall_state const &base,
	                                    core_recall_state &out) {
		std::vector<uint16_t> ram, samples;
		vst3::CoreMidiSection::State midi_wire;
		vst3::CoreExecutionSection::State cpu_wire;
		if (!vst3::CoreCheckpointBundle::decode(
			data, size, ram, samples, midi_wire, cpu_wire)) return false;
		if (ram.size() != vst3::MachineMemoryCodec::ram_words ||
		    samples.size() != vst3::MachineMemoryCodec::sample_words) return false;
		try {
			auto staged = std::make_unique<core_recall_state>(base);
			midi_tx_t tx;
			tx.next = midi_wire.next;
			tx.q_head = midi_wire.head;
			tx.bit = midi_wire.bit;
			tx.cur = midi_wire.current;
			tx.q = std::move(midi_wire.queue);
			for (auto &event : midi_wire.events)
				tx.ev.emplace_back(event.time, std::move(event.bytes));
			staged->motherboard_ram.swap(ram);
			staged->sample_ram.swap(samples);
			staged->midi = std::move(tx);
			staged->cpu.pc = cpu_wire.pc;
			staged->cpu_clock.completed_cycles = cpu_wire.completed_cycles;
			out = std::move(*staged);
		} catch (...) { return false; }
		return true;
	}
	// Serialize stopped CPU position/timebase; not the complete CPU register file.
	static bool encode_execution_core_recall(core_recall_state const &state,
	                                         std::vector<uint8_t> &out) {
		vst3::CoreExecutionSection::State entry;
		entry.pc = state.cpu.pc;
		entry.completed_cycles = state.cpu_clock.completed_cycles;
		return vst3::CoreExecutionSection::encode(entry, out);
	}
	// Decode PC/cycle subsection into a detached, pre-existing full-state
	// candidate. Never restore a running CPU from this incomplete section.
	static bool stage_execution_core_recall(uint8_t const *data, size_t size,
	                                        core_recall_state &candidate) {
		vst3::CoreExecutionSection::State decoded;
		if (!vst3::CoreExecutionSection::decode(data, size, decoded))
			return false;
		candidate.cpu.pc = decoded.pc;
		candidate.cpu_clock.completed_cycles = decoded.completed_cycles;
		return true;
	}
	// Adapt the owner-thread core snapshot to the portable partial transport.
	// This is deliberately not exposed as a complete machine recall: all
	// non-RAM/MIDI device fields still require their own portable encoders.
	static bool capture_partial_transport(core_recall_state const &state,
	                                      vst3::CoreMidiSection::State &midi_out) {
		if (state.midi.next > UINT32_MAX || state.midi.q_head > UINT32_MAX ||
		    state.midi.ev.size() > UINT32_MAX) return false;
		vst3::CoreMidiSection::State converted;
		converted.next = uint32_t(state.midi.next);
		converted.head = uint32_t(state.midi.q_head);
		converted.bit = state.midi.bit;
		converted.current = state.midi.cur;
		converted.queue.assign(state.midi.q.begin(), state.midi.q.end());
		for (auto const &event : state.midi.ev)
			converted.events.push_back({event.first, event.second});
		midi_out = std::move(converted);
		return true;
	}
	static bool encode_partial_core_recall(core_recall_state const &state,
	                                       std::vector<uint8_t> &out) {
		vst3::CoreMidiSection::State midi_state;
		if (!capture_partial_transport(state, midi_state)) return false;
		return vst3::CorePartialSnapshot::encode(
			state.motherboard_ram.data(), state.motherboard_ram.size(),
			state.sample_ram.data(), state.sample_ram.size(),
			midi_state, out);
	}
	// Decode only the portable subsections into value-owned staging outputs.
	// Never apply partial data to a live machine or call restore_core_recall:
	// CPU, scheduler, SCSI and renderer are absent from this wire format.
	static bool decode_partial_core_recall(uint8_t const *data, size_t size,
	                                       std::vector<uint16_t> &ram_out,
	                                       std::vector<uint16_t> &samples_out,
	                                       midi_tx_t &midi_out) {
		std::vector<uint16_t> ram, samples;
		vst3::CoreMidiSection::State decoded;
		if (!vst3::CorePartialSnapshot::decode(data, size, ram, samples, decoded))
			return false;
		midi_tx_t tx;
		tx.next = decoded.next;
		tx.q_head = decoded.head;
		tx.bit = decoded.bit;
		tx.cur = decoded.current;
		tx.q.assign(decoded.queue.begin(), decoded.queue.end());
		for (auto const &event : decoded.events)
			tx.ev.emplace_back(event.time, event.bytes);
		ram_out.swap(ram);
		samples_out.swap(samples);
		midi_out.ev.swap(tx.ev);
		midi_out.q.swap(tx.q);
		midi_out.next = tx.next;
		midi_out.q_head = tx.q_head;
		midi_out.bit = tx.bit;
		midi_out.cur = tx.cur;
		return true;
	}
	// Merge decoded partial transport only into an independently owned staging
	// snapshot. Callers must supply the remaining CPU/device state from a
	// complete, validated source; this cannot be used as a live restore API.
	static bool stage_partial_core_recall(uint8_t const *data, size_t size,
	                                      core_recall_state const &base,
	                                      core_recall_state &out) {
		std::vector<uint16_t> ram, samples;
		midi_tx_t tx;
		if (!decode_partial_core_recall(data, size, ram, samples, tx))
			return false;
		if (ram.size() != vst3::MachineMemoryCodec::ram_words ||
		    samples.size() != vst3::MachineMemoryCodec::sample_words)
			return false;
		try {
			auto staged = std::make_unique<core_recall_state>(base);
			staged->motherboard_ram.swap(ram);
			staged->sample_ram.swap(samples);
			staged->midi = std::move(tx);
			out = std::move(*staged);
		} catch (...) {
			return false;
		}
		return true;
	}
	bool capture_core_recall(core_recall_state &out) const {
		if (!powered || stop || !cpu || !fpu || sched.m_cpu_running) return false;
		auto tmp = std::make_unique<core_recall_state>();
		shim::image_fingerprint rom_hash;
		for(auto word:m.rom) {
			uint8_t bytes[]={uint8_t(word),uint8_t(word>>8)};
			rom_hash.update(bytes,2);
		}
		tmp->context.rom=rom_hash.full;
		for(unsigned id=0;id<8;++id) {
			auto* image=hd_image(id);
			if(!image)continue;
			auto overlay=image->export_changes();
			shim::image_fingerprint hash;hash.update(overlay.data(),overlay.size());
			tmp->context.overlays[id]=hash.full;
		}
		tmp->context.pit1_last=m.pit1_last;
		tmp->context.sync_pattern={scc_cha->recall_sync_pattern(),scc_chb->recall_sync_pattern()};
		tmp->context.rr7p={scc_cha->m_rr7p,scc_chb->m_rr7p};
		tmp->motherboard_ram.assign(m.ram.begin(),m.ram.end());
		tmp->sample_ram.assign(m.snd.begin(),m.snd.end());
		tmp->motherboard = m.capture_motherboard_recall();
		if (!machine_t::valid_motherboard_recall(tmp->motherboard)) return false;
		tmp->cpu = cpu->capture_recall_state();
		if (!cpu->capture_clock_recall(tmp->cpu_clock)) return false;
		tmp->fpu = fpu->capture_recall_state();
		if (!sched.capture_recall_state(tmp->scheduler)) return false;
		// In the pre-D0 topology, MIDI/panel and every original device were
		// registered before the WD1772 and drive. Verify that exact order
		// against live timer pointers; never infer a migration from count alone.
		if(fdd && fdc && sched.m_timers.size()>=5) {
			auto handles=fdc->recall_timer_handles();
			auto const count=sched.m_timers.size();
			tmp->d0_timers_at_tail=sched.m_timers[count-5]==fdd->index_timer_handle();
			for(unsigned i=0;i<4;++i)
				tmp->d0_timers_at_tail &= sched.m_timers[count-4+i]==handles[i];
		}
		// Capture D0 head/rotation/WD1772 live decoder and digital PLL together
		// with the machine scheduler. Never serialize MAME callbacks or media.
		tmp->d0_present=!cfg.floppy_path.empty();
		if(tmp->d0_present) {
			if(!fdd||!fdc)return false;
			tmp->d0.drive=fdd->capture_recall_state();
			tmp->d0.controller=fdc->capture_recall_state();
			if(!fdd->valid_recall_state(tmp->d0.drive)||
			   !fdc->valid_recall_state(tmp->d0.controller))return false;
		}
		if (!scsi || !hdc || !scsi->capture_recall_state(tmp->scsi_bus) ||
		    !hdc->capture_recall_state(tmp->ncr) ||
		    !backend || !backend->capture_recall_state(tmp->audio)) return false;
		// Capture every configured target at this same stopped boundary.
		for (unsigned id=0; id<8; ++id) {
			auto *disk = id==1 ? hd.get() : (id>=4 ? extra[id].hd.get() : nullptr);
			auto *optical = id==3 ? cd.get() : (id>=4 ? extra[id].cd.get() : nullptr);
			if (disk) {
				tmp->disk_present[id]=true;
				if (!disk->capture_disk_target_recall(tmp->disks[id])) return false;
			}
			if (optical) {
				tmp->cd_present[id]=true;
				if (!optical->capture_cd_target_recall(tmp->cds[id])) return false;
			}
		}
		tmp->midi = midi;
		if (!scc || !scc_cha || !scc_chb) return false;
		tmp->serial={scc_cha->capture_serial_recall(),scc_chb->capture_serial_recall()};
		tmp->scc_controller=scc->capture_controller_recall();
		tmp->scc_a_fifo=scc_cha->capture_fifo_recall();
		tmp->scc_b_fifo=scc_chb->capture_fifo_recall();
		tmp->scc_a_regs=scc_cha->capture_register_recall();
		tmp->scc_b_regs=scc_chb->capture_register_recall();
		tmp->scc_a_line=scc_cha->capture_channel_line_recall();
		tmp->scc_b_line=scc_chb->capture_channel_line_recall();
		if(!valid_core_recall(*tmp))return false;
		out = std::move(*tmp);
		return true;
	}
	// Runtime renderer starts at t20=0 and adds one fixed binary64 step per
	// model sample. Validate its integer frame counter against that clock.
	// Standard summation error: |fl(sum)-N*step| <= gamma_N*|N*step|,
	// gamma_N=N*u/(1-N*u), u=epsilon/2. Each bound operation rounds outward;
	// there is no guessed sample-count tolerance and no change to rendering.
	static bool valid_audio_timeline(software_backend::recall_state const& audio) {
		if(!software_backend::valid_recall_state(audio))return false;
		unsigned phase=audio.model_v2?audio.decimator.phase():audio.decimation_phase;
		if(audio.output_frames>(UINT64_MAX-phase)/audio.oversample)return false;
		uint64_t samples=audio.output_frames*audio.oversample+phase;
		if(samples==0)return audio.next==0 && audio.now==0;
		if(samples>UINT64_C(9007199254740992))return false; // exact binary64 integers
		double step=20e6/audio.sample_rate;
		double expected=double(samples)*step;
		if(!std::isfinite(step)||step<=0||!std::isfinite(expected))return false;
		auto up=[](double x){return std::nextafter(x,std::numeric_limits<double>::infinity());};
		constexpr double u=std::numeric_limits<double>::epsilon()/2;
		double nu=up(double(samples)*u);
		if(nu>=1)return false;
		double denominator=std::nextafter(1-nu,0.0);
		double gamma=up(nu/denominator);
		double relative=up(up(gamma+u)/std::nextafter(1-u,0.0));
		double error=up(relative*std::abs(expected));
		double difference=std::nextafter(std::abs(audio.next-expected),0.0);
		return difference<=error && audio.next>=double(audio.now) &&
		       audio.next<=up(double(audio.now)+step);
	}
	bool valid_core_recall(core_recall_state const &state) const {
		if (!powered || stop || !cpu || !fpu || !scc || !scc_cha || !scc_chb || sched.m_cpu_running) return false;
		// Validate all recoverable failures before mutating any device.
		if (state.motherboard_ram.size() != m.ram.size() ||
		    state.sample_ram.size() != m.snd.size() ||
		    !machine_t::valid_motherboard_recall(state.motherboard) ||
		    !cpu->clock_recall_ready()) return false;
		if (!scsi || !hdc || !scsi->valid_recall_state(state.scsi_bus) ||
		    !ncr5380_device::valid_recall_state(state.ncr) ||
		    !backend || !valid_audio_timeline(state.audio) ||
		    state.cpu_clock.completed_cycles>UINT64_MAX/2 ||
		    state.audio.now>state.cpu_clock.completed_cycles*2) return false;
		// Media identity, topology and all target cursors must pass before ANY
		// component is mutated. Never silently skip an optional target.
		for (unsigned id=0; id<8; ++id) {
			auto *disk = id==1 ? hd.get() : (id>=4 ? extra[id].hd.get() : nullptr);
			auto *optical = id==3 ? cd.get() : (id>=4 ? extra[id].cd.get() : nullptr);
			if (state.disk_present[id] != (disk != nullptr) ||
			    state.cd_present[id] != (optical != nullptr)) return false;
			if (disk && !disk->valid_disk_target_recall(state.disks[id])) {fprintf(stderr,"B02 HDD D%u rejected\n",id);return false;}
			if (optical && !optical->valid_cd_target_recall(state.cds[id])) {fprintf(stderr,"B02 CD D%u rejected\n",id);return false;}
		}
		if(state.d0_present!=!cfg.floppy_path.empty())return false;
		if(state.d0_present && (!fdd||!fdc||
		   !fdd->valid_recall_state(state.d0.drive)||
		   !fdc->valid_recall_state(state.d0.controller)))return false;
		if (!sched.valid_recall_state(state.scheduler)) return false;
		if(state.fpu.state!=0 && state.fpu.state!=1 && state.fpu.state!=2 &&
		   state.fpu.state!=4 && state.fpu.state!=5)return false;
		for (auto const &op : state.fpu.op)
			if (op.issued > op.expected || op.expected > 8) return false;
		// MIDI transmitter has queued byte packets, event cursor and a
		// partially transmitted serial byte. Reject corrupt cursors first.
		auto const &tx = state.midi;
		if (tx.next > tx.ev.size() || tx.q_head > tx.q.size() ||
		    tx.bit < -1 || tx.bit > 8) return false;
		for (auto const &event : tx.ev)
			if (!std::isfinite(event.first) || event.first < 0) return false;
		for (size_t i = 1; i < tx.ev.size(); ++i)
			if (!(tx.ev[i-1].first <= tx.ev[i].first)) return false;
		if (!z80scc_channel::valid_fifo_cursors(state.scc_a_fifo.cursors) ||
		    !z80scc_channel::valid_fifo_cursors(state.scc_b_fifo.cursors)) return false;
		auto const &ac=state.scc_a_fifo.cursors;
		auto const &bc=state.scc_b_fifo.cursors;
		if (ac.rx_capacity != scc_cha->capture_fifo_cursors().rx_capacity ||
		    ac.tx_capacity != scc_cha->capture_fifo_cursors().tx_capacity ||
		    bc.rx_capacity != scc_chb->capture_fifo_cursors().rx_capacity ||
		    bc.tx_capacity != scc_chb->capture_fifo_cursors().tx_capacity) return false;
		if (!z80scc_device::valid_controller_recall(state.scc_controller)) return false;
		// All allocating operations precede mutation of CPU/FPU/timers.
		for(auto const& channel:state.serial)
			if(!device_serial_interface::valid_serial_recall(channel))return false;
		return true;
	}
	bool apply_core_recall_preflighted(core_recall_state const &state) {
		if(!valid_core_recall(state))return false;
		midi_tx_t prepared_midi = state.midi;
		// No callbacks fire during assignment; timer pointers stay instance-local.
		// Renderer restoration can allocate; complete it before mutating
		// CPU, timers, SCSI or MIDI. The remaining operations were preflighted.
		// Preserve destination-owned RAM storage: the renderer holds a pointer
		// to it. Never replace/reallocate that storage during restore.
		if (!backend->restore_recall_state(state.audio)) return false;
		std::copy(state.motherboard_ram.begin(),state.motherboard_ram.end(),m.ram.begin());
		std::copy(state.sample_ram.begin(),state.sample_ram.end(),m.snd.begin());
		if (!sched.restore_recall_state(state.scheduler)) return false;
		if(state.d0_present && (!fdd->restore_recall_state(state.d0.drive)||
		   !fdc->restore_recall_state(state.d0.controller)))return false;
		cpu->restore_recall_state(state.cpu);
		if (!cpu->restore_clock_recall(state.cpu_clock)) return false;
		// Register-level restore only: no I/O handlers, PIT or IRQ callbacks.
		// The SCSI and audio subsections are restored separately below.
		if (!m.restore_motherboard_recall(state.motherboard)) return false;
		if (!fpu->restore_recall_state(state.fpu)) return false;
		// Restore bus lines without edge notifications; target-device internals
		// follow below before candidate verification and any CPU execution.
		if (!scsi->restore_recall_state(state.scsi_bus) ||
		    !hdc->restore_recall_state(state.ncr)) return false;
		// Restore target-side command buffers without running commands. Bus/NCR
		// edges remain suppressed; the scheduler is still stopped.
		for (unsigned id=0; id<8; ++id) {
			auto *disk = id==1 ? hd.get() : (id>=4 ? extra[id].hd.get() : nullptr);
			auto *optical = id==3 ? cd.get() : (id>=4 ? extra[id].cd.get() : nullptr);
			if (disk && !disk->restore_disk_target_recall(state.disks[id])) return false;
			if (optical && !optical->restore_cd_target_recall(state.cds[id])) return false;
		}
		if(!scc_cha->restore_serial_recall(state.serial[0]) ||
		   !scc_chb->restore_serial_recall(state.serial[1]))return false;
		m.pit1_last=state.context.pit1_last;
		scc_cha->restore_sync_pattern(state.context.sync_pattern[0]);
		scc_chb->restore_sync_pattern(state.context.sync_pattern[1]);
		scc_cha->m_rr7p=state.context.rr7p[0];scc_chb->m_rr7p=state.context.rr7p[1];
		midi = std::move(prepared_midi);
		scc_cha->restore_register_recall(state.scc_a_regs);
		scc_chb->restore_register_recall(state.scc_b_regs);
		scc_cha->restore_channel_line_recall(state.scc_a_line);
		scc_chb->restore_channel_line_recall(state.scc_b_line);
		if (!scc_cha->restore_fifo_recall(state.scc_a_fifo) ||
		    !scc_chb->restore_fifo_recall(state.scc_b_fifo)) return false;
		return scc->restore_controller_recall(state.scc_controller);
	}

	// Transaction boundary: capture the entire live destination before applying
	// a foreign snapshot. A failed application must never silently leave a
	// partially restored instance able to continue executing.
	// Validate all scalar/volatile fields we can compare without invoking
	// callbacks or allocating. Reused for success and rollback verification.
	static bool same_scheduler_checkpoint(shim_scheduler::recall_state const &a,
	                                      shim_scheduler::recall_state const &b) {
		if (a.basetime != b.basetime || a.timers.size() != b.timers.size()) return false;
		for (size_t i=0; i<a.timers.size(); ++i) {
			auto const &x=a.timers[i], &y=b.timers[i];
			if (x.expire != y.expire || x.start != y.start ||
			    x.period != y.period || x.param != y.param ||
			    x.enabled != y.enabled) return false;
		}
		return true;
	}
	static bool same_core_checkpoint(core_recall_state const &a,
	                                 core_recall_state const &b) {
		std::vector<uint8_t> left,right;
		return encode_core_device_sections(a,left) &&
		       encode_core_device_sections(b,right) && left==right;
	}
	bool restore_core_recall(core_recall_state const &state) {
		if(!valid_core_recall(state))return false;
		// Snapshot allocation may fail under memory pressure. No destination
		// state has been mutated yet, so fail safely without propagating.
		std::unique_ptr<core_recall_state> before;
		try {
			before = std::make_unique<core_recall_state>();
			if (!capture_core_recall(*before)) return false;
			if(state.context.rom!=before->context.rom || state.context.overlays!=before->context.overlays)return false;
		} catch (...) {
			return false;
		}
		try {
			if (apply_core_recall_preflighted(state)) {
				// Device APIs returning success are not proof that the restored
				// machine contains the requested volatile state. Check before
				// exposing the instance for further emulation.
				auto verified = std::make_unique<core_recall_state>();
				if (capture_core_recall(*verified) &&
				    same_core_checkpoint(*verified, state))
					return true;
			}
		} catch (...) {
			// An allocation/host exception may have followed mutation.
		}
		try {
			if (apply_core_recall_preflighted(*before)) {
				// A successful component-level rollback is not sufficient: at
				// least verify all volatile RAM and CPU execution position again.
				// Any mismatch makes this runtime unsafe to resume.
				auto verified = std::make_unique<core_recall_state>();
				if (capture_core_recall(*verified) &&
				    same_core_checkpoint(*verified, *before))
					return false;
			}
		} catch (...) {}
		// If rollback itself fails, refuse further execution. This is not
		// claimed as atomic success; the instance must be discarded.
		stop = 2;
		return false;
	}

};

} // namespace eiii
