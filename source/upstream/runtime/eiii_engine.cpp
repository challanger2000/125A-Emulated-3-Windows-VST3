// eiii::engine — see eiii_engine.h.
#include "eiii_engine.h"
#include "../standalone/shim/utf8_file_paths.h"
#include "eiii_bank_import.h"
#include "wav_firmware_import.h"
#include "latency_policy.h"
#include "adc_quantize.h"
#include "panel/eiii_lcd.h"
#include "panel/eiii_panel_model.h"
#include "standalone/ui_macro.h"
#include "standalone/first_start_midi.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <cstdio>
#include <cstdlib>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <pthread.h>
#include <sched.h>
#endif
#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)
#include <xmmintrin.h>
#endif

namespace eiii {

namespace {
int64_t now_ns() { return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
void set_ftz()
{
	// as the standalone: flush denormals in the analog model (outputs proven identical, see check_runtime.sh)
#if defined(__x86_64__) || defined(__i386__)
	_mm_setcsr(_mm_getcsr() | 0x8040);
#elif defined(__aarch64__)
	uint64_t fpcr; __asm__ volatile("mrs %0, fpcr" : "=r"(fpcr)); fpcr |= (1ull << 24); __asm__ volatile("msr fpcr, %0" : : "r"(fpcr));
#endif
}
}

struct media_request {
 media_result r;
 std::function<void(runtime &)> fn;
 wav::Options wav_options;
 engine_config project_config;
 std::function<void()> on_project_commit;
 std::vector<uint8_t> project_snapshot;
 // Single detached preparation thread per engine; never reads the live runtime.
 std::unique_ptr<runtime> prepared_project;
 std::string preparation_error;
 bool preparation_ok=false;
 std::atomic<bool> preparation_done{false};
 std::thread preparer;
 ~media_request() {if(preparer.joinable())preparer.join();}
};

static void prepare_project(media_request& rq)
{
 try {
  auto const& cfg=rq.project_config.rt;
  auto& error=rq.preparation_error;
  bool ok=true;
  if(!cfg.rom_dir.empty())ok=runtime::check_rom_dir(cfg.rom_dir,error);
  if(ok&&!cfg.hd_path.empty())ok=runtime::check_hd_image(cfg.hd_path,error);
  if(ok&&!cfg.cd_path.empty())ok=runtime::check_cd_image(cfg.cd_path,error);
  for(unsigned id=4;ok&&id<=7;++id) {
   auto const& target=cfg.scsi[id];
   if(target.kind==scsi_kind::hd)ok=runtime::check_hd_image(target.path,error);
   else if(target.kind==scsi_kind::cd&&!target.path.empty())ok=runtime::check_cd_image(target.path,error);
   else if(target.kind!=scsi_kind::none&&target.kind!=scsi_kind::cd) {
    ok=false;error="unsupported restored SCSI type";
   }
  }
  const bool configured=!cfg.rom_dir.empty()&&!cfg.hd_path.empty();
  if(ok&&!configured&&!rq.project_snapshot.empty()) {ok=false;error="saved machine requires ROM and OS media";}
  if(ok&&configured) {
   auto candidate=std::make_unique<runtime>();
   ok=rq.project_snapshot.empty()
     ?candidate->open(cfg,error)
     :candidate->open_restored(cfg,rq.project_snapshot.data(),rq.project_snapshot.size(),error);
   if(ok&&!candidate->media_restore_valid()) {ok=false;error=candidate->open_warning();}
   if(ok)rq.prepared_project=std::move(candidate);
  }
  rq.preparation_ok=ok;
 } catch(std::exception const& e) {
  rq.preparation_ok=false;rq.preparation_error=e.what();
 } catch(...) {
  rq.preparation_ok=false;rq.preparation_error="candidate preparation failed";
 }
}


char const *media_slot_name(media_slot s)
{
	switch (s) { case media_slot::cd: return "cd"; case media_slot::hd: return "hd"; case media_slot::rom: return "rom_dir";
	case media_slot::floppy: return "floppy"; case media_slot::scsi: return "scsi"; case media_slot::cem_model: return "cem_model"; case media_slot::restart: return "restart"; case media_slot::inspect: return "inspect"; case media_slot::hd_changes: return "hd_changes"; case media_slot::converted_cd: return "converted_cd"; case media_slot::wav_import: return "wav_import"; case media_slot::project_restore: return "project_restore"; }
	return "?";
}

engine::engine() = default;
engine::~engine()
{
	shutdown();
	media_request *rq;
	while (m_media_in.pop(rq)) delete rq;
	while (m_media_out.pop(rq)) delete rq;
}

bool engine::queue_media(media_request *rq)
{
	if (m_wav_busy.load() && rq->r.slot != media_slot::wav_import) { delete rq; return false; }
	if (m_media_inflight >= 8) { delete rq; return false; }
	if (!m_thread.joinable())
	{
		apply_media(*rq, false);                     // no engine thread: only the configuration of the next start changes
		m_media_out.push(rq);
	}
	else m_media_in.push(rq);
	++m_media_inflight;
	return true;
}

bool engine::set_media(media_slot slot, std::string const &path)
{
	auto *rq = new media_request;
	rq->r.slot = slot; rq->r.path = path;
	if (slot == media_slot::converted_cd) { rq->r.scsi_id = 3; rq->r.kind = scsi_kind::cd; }
	if (slot == media_slot::cd) { rq->r.slot = media_slot::scsi; rq->r.scsi_id = 3; rq->r.kind = scsi_kind::cd; }
	if (slot == media_slot::hd) { rq->r.slot = media_slot::scsi; rq->r.scsi_id = 1; rq->r.kind = scsi_kind::hd; }
	return queue_media(rq);
}

bool engine::set_scsi(unsigned id, scsi_kind kind, std::string const &path)
{
	auto *rq = new media_request;
	rq->r.slot = media_slot::scsi; rq->r.scsi_id = uint8_t(id); rq->r.kind = kind; rq->r.path = path;
	return queue_media(rq);
}

bool engine::import_wav(const std::string& path, const wav::Options& options)
{
    if (!options.valid() || !started() || m_quit.load() || m_wav_busy.load()) return false;
    auto request = std::make_unique<media_request>();
    request->r.slot = media_slot::wav_import; request->r.path = path; request->wav_options = options;
    m_wav_busy.store(true);m_wav_cancel.store(false);
    if (!queue_media(request.release())) { m_wav_busy.store(false); return false; }
    return true;
}

bool engine::poll_media_result(media_result &out)
{
	media_request *rq;
	if (!m_media_out.pop(rq)) return false;
	--m_media_inflight;
	out = std::move(rq->r);
	// A successful project restore replaces *all* media references together.
	// Publish the same configuration the worker just accepted before deleting it.
	if (out.ok && out.slot == media_slot::project_restore) m_view = rq->project_config.rt;
	delete rq;
	if (out.ok)
		switch (out.slot)
		{
		case media_slot::rom: m_view.rom_dir = out.path; break;
		case media_slot::floppy: m_view.floppy_path = out.path; break;
		case media_slot::converted_cd: m_view.cd_path = out.path; break;
		case media_slot::cem_model: m_view.cem_model = out.path; break;
		case media_slot::scsi:
			if (out.scsi_id == 1) m_view.hd_path = out.path;
			else if (out.scsi_id == 3) m_view.cd_path = out.path;
			else if (out.scsi_id < 8) m_view.scsi[out.scsi_id] = { out.kind, out.kind == scsi_kind::none ? std::string() : out.path };
			break;
		default: break;
		}
	return true;
}

bool engine::set_hd_changes(unsigned id, std::string const &changes)
{
	auto *rq = new media_request;
	rq->r.slot = media_slot::hd_changes; rq->r.scsi_id = uint8_t(id); rq->r.path = changes;
	return queue_media(rq);
}

uint64_t engine::invalidate_project_restores(uint64_t generation)
{
 std::lock_guard<std::mutex> lock(m_project_mutex);
 m_project_generation=generation;
 return m_committed_project_generation;
}

bool engine::restore_project(engine_config const& config, std::function<void()> on_commit, uint64_t generation, std::vector<uint8_t> snapshot)
{
 if (!started() || m_quit.load() || wav_import_busy() ||
     snapshot.size() > 192u*1024u*1024u+20u) return false;
 auto rq=std::make_unique<media_request>();
 rq->r.slot=media_slot::project_restore;
 rq->r.project_generation=generation;
 rq->project_config=config;
 rq->project_snapshot=std::move(snapshot);
 rq->on_project_commit=std::move(on_commit);
 rq->project_config.first_start_midi_omni=false; // saved song always wins over new-instance defaults
 return queue_media(rq.release());
}

bool engine::refresh_hd_changes()
{
 if (!m_thread.joinable()) return true;
 const uint64_t requested=m_hd_refresh_req.fetch_add(1, std::memory_order_acq_rel)+1;
 const auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(500);
 while (m_hd_refresh_done.load(std::memory_order_acquire)<requested) {
  if (m_quit.load() || std::chrono::steady_clock::now()>=deadline) return false;
  std::this_thread::sleep_for(std::chrono::milliseconds(1));
 }
 return true;
}

std::string engine::hd_changes(unsigned id) const { std::lock_guard<std::mutex> g(m_hd_mutex); return id < 8 ? m_hd_pub[id] : std::string(); }
std::string engine::take_warning() { std::lock_guard<std::mutex> g(m_hd_mutex); std::string w; w.swap(m_warning); return w; }

void engine::keep_hd_changes()
{
	if (!m_rt) return;
	for (unsigned id = 1; id < 8; ++id)
		if (m_rt->drive_kind(id) == scsi_kind::hd) m_cfg.rt.hd_changes[id] = m_rt->hd_changed_sectors(id) ? m_rt->hd_changes(id) : std::string();
}

// all: right after a boot (the change sets now in use); else the ones the OS changed, once writing has paused
void engine::publish_hd_changes(bool all)
{
	int64_t const now = now_ns();
	for (unsigned id = 1; id < 8; ++id)
	{
		bool const hd = m_rt && m_rt->drive_kind(id) == scsi_kind::hd;
		if (all)
		{
			std::string const blob = hd && m_rt->hd_changed_sectors(id) ? m_rt->hd_changes(id) : std::string();
			m_hd_seen[id] = hd ? m_rt->hd_writes(id) : 0; m_hd_dirty_ns[id] = 0;
			std::lock_guard<std::mutex> g(m_hd_mutex);
			if (m_hd_pub[id] != blob) { m_hd_pub[id] = blob; ++m_hd_serial; }
			continue;
		}
		if (!hd) continue;
		uint64_t const w = m_rt->hd_writes(id);
		if (w != m_hd_seen[id]) {
			// Writes performed by firmware are not the same as a boot, image mount
			// or host-overlay restore. Count these without copying the overlay.
			if (w > m_hd_seen[id])
				m_hd_firmware_write_serial.fetch_add(1,std::memory_order_release);
			m_hd_seen[id] = w; m_hd_dirty_ns[id] = now; continue;
		}
		if (m_hd_dirty_ns[id] && now - m_hd_dirty_ns[id] > 300'000'000)
		{
			m_hd_dirty_ns[id] = 0;
			std::string blob = m_rt->hd_changes(id);
			std::lock_guard<std::mutex> g(m_hd_mutex);
			m_hd_pub[id].swap(blob); ++m_hd_serial;
		}
	}
}

bool engine::inspect(std::function<void(runtime &)> f)
{
	auto *rq = new media_request;
	rq->r.slot = media_slot::inspect; rq->fn = std::move(f);
	return queue_media(rq);
}

// B02 partial snapshot only. Shared completion storage avoids dangling caller
// references if the worker is delayed beyond the bounded wait.
// B02 diagnostic capture. Never call from the audio callback or worker thread.
bool engine::capture_device_sections(std::vector<uint8_t>& out)
{
	runtime_config config;
	return capture_project(out,config);
}

bool engine::capture_project(std::vector<uint8_t>& out, runtime_config& config)
{
	if (!m_thread.joinable() || m_quit.load() || firmware_setup_busy()) return false;
	struct completion {
		std::mutex mutex;
		std::condition_variable cv;
		bool done=false,ok=false;
		std::vector<uint8_t> bytes;
		runtime_config config;
	};
	auto state=std::make_shared<completion>();
	if (!inspect([this,state](runtime& rt) {
		std::vector<uint8_t> candidate;
		bool ok=false;
		try {
			ok=!firmware_setup_busy() && rt.capture_device_sections(candidate);
			if(ok)state->config=rt.project_configuration();
		} catch (...) { ok=false; }
		{
			std::lock_guard<std::mutex> lock(state->mutex);
			if(ok)state->bytes.swap(candidate);
			state->ok=ok;
			state->done=true;
		}
		state->cv.notify_one();
	})) return false;
	std::unique_lock<std::mutex> lock(state->mutex);
	if(!state->cv.wait_for(lock,std::chrono::seconds(2),
	                       [&]{return state->done||m_quit.load();})||
	   !state->done||!state->ok)return false;
	out.swap(state->bytes);
	config=std::move(state->config);
	return true;
}

bool engine::capture_volatile_memory(std::vector<uint16_t>& ram,
                                     std::vector<uint16_t>& samples)
{
	if (!m_thread.joinable() || m_quit.load()) return false;
	struct completion {
		std::mutex mutex;
		std::condition_variable cv;
		bool done=false, ok=false;
		std::vector<uint16_t> ram, samples;
	};
	auto state=std::make_shared<completion>();
	if (!inspect([state](runtime& rt) {
		std::vector<uint16_t> r,s;
		bool ok=false;
		try { ok=rt.copy_volatile_memory(r,s); } catch (...) { ok=false; }
		{
			std::lock_guard<std::mutex> lock(state->mutex);
			if (ok) { state->ram.swap(r); state->samples.swap(s); }
			state->ok=ok;
			state->done=true;
		}
		state->cv.notify_one();
	})) return false;
	std::unique_lock<std::mutex> lock(state->mutex);
	if (!state->cv.wait_for(lock,std::chrono::seconds(2),
	                       [&]{return state->done || m_quit.load();}) ||
	    !state->done || !state->ok) return false;
	ram.swap(state->ram);
	samples.swap(state->samples);
	return true;
}

// ---- sampling input ----
void engine::input(float const *left, float const *right, size_t frames)
{
	if (m_wav_busy.load()) return;
	// the block's first frame is at input index = ring read index (ring frame - latency): pad / skip to stay aligned
	uint64_t const want = m_ring.read_index();
	float tmp[2 * 256];
	uint64_t w = m_in.write_index();
	while (w < want && m_in.space())
	{
		size_t const n = size_t(std::min<uint64_t>({ want - w, 256, m_in.space() }));
		std::fill(tmp, tmp + 2 * n, 0.0f);
		w += m_in.write(tmp, n);
	}
	size_t i = w > want ? size_t(std::min<uint64_t>(frames, w - want)) : 0;
	while (i < frames && m_in.space())
	{
		size_t const n = std::min<size_t>({ frames - i, 256, m_in.space() });
		auto clean = [](float v) { return std::fabs(v) < 1e-20f ? 0.0f : v; };   // no denormals / tiny values downstream
		for (size_t k = 0; k < n; ++k) { tmp[2 * k] = left ? clean(left[i + k]) : 0.0f; tmp[2 * k + 1] = right ? clean(right[i + k]) : left ? clean(left[i + k]) : 0.0f; }
		m_in.write(tmp, n);
		i += n;
	}
}

// engine thread: move new input frames into m_in_buf, drop what the emulation has passed
void engine::drain_input()
{
	float l[256], r[256];
	for (;;)
	{
		size_t const n = m_in.read(l, r, 256);
		if (!n) break;
		for (size_t k = 0; k < n; ++k) { m_in_buf.push_back(l[k]); m_in_buf.push_back(r[k]); }
		m_in_end += n;
	}
	uint64_t const base = m_in_end - m_in_buf.size() / 2;
	int64_t const cur = int64_t(m_emu_frames) - m_ring_to_emu - int64_t(latency()) - 64;   // input index now in use
	if (!m_rt) { m_in_buf.clear(); return; }
	if (cur > int64_t(base))
	{
		size_t const drop = size_t(std::min<uint64_t>(uint64_t(cur) - base, m_in_buf.size() / 2));
		m_in_buf.erase(m_in_buf.begin(), m_in_buf.begin() + long(2 * drop));
	}
}

// engine thread, inside the emulation: the analog input at a conversion (emulated seconds)
void engine::adc_sample(double t, int16_t lr[2])
{
	lr[0] = lr[1] = 0;
	if (!m_rt) return;
	double const idx = t * m_cfg.rt.sample_rate - double(m_ring_to_emu) - double(latency());   // input index
	// the audio thread hands over each block's input before it pulls that block, so frames the emulation may reach
	// are in the ring by now; fetch them if this step started before they arrived
	if (idx + 1 >= double(m_in_end)) drain_input();
	if (m_in_buf.size() < 4) return;
	double const pos = idx - double(m_in_end - m_in_buf.size() / 2);
	if (pos < 0 || pos >= double(m_in_buf.size() / 2 - 1)) return;
	size_t const i = size_t(pos);
	float const f = float(pos - double(i));
	// input level: the Sample Input slider (ADC multiplexer X0).  The original dbx 2155 gain law is not documented;
	// assumed: centre of travel = unity, +-20 dB at the ends (log taper), silent at the very bottom.
	double const p = double(m_rt->adc_input(0)) / double(0x7100);
	double const gain = p < 0.01 ? 0.0 : std::pow(10.0, (std::min(p, 1.0) - 0.5) * 2.0);
	for (int c = 0; c < 2; ++c)
	{
		float const x = m_in_buf[2 * i + c] + (m_in_buf[2 * (i + 1) + c] - m_in_buf[2 * i + c]) * f;
		lr[c] = eiii::adc::quantize(double(x), gain);
	}
}

std::string engine::media_path(media_slot slot) const
{
	switch (slot)
	{
	case media_slot::floppy: return m_view.floppy_path;
	case media_slot::cd: return m_view.cd_path;
	case media_slot::hd: return m_view.hd_path;
	case media_slot::rom: return m_view.rom_dir;
	case media_slot::cem_model: return m_view.cem_model;
	default: return {};
	}
}

scsi_target engine::scsi(unsigned id) const
{
	if (id == 1) return { scsi_kind::hd, m_view.hd_path };
	if (id == 3) return { scsi_kind::cd, m_view.cd_path };
	return id < 8 && scsi_extra_id(id) ? m_view.scsi[id] : scsi_target{};
}

// engine thread (live) or main thread with no engine thread (configuration only)
void engine::apply_media(media_request &rq, bool live)
{
	media_result &r = rq.r;
	runtime_config &c = m_cfg.rt;
	std::string err;
	bool restart = false, ok = false;
	runtime *const rt = live ? m_rt.get() : nullptr;
	// A late project restore has a separate prepare/commit path below. Never
	// change the running machine or its disk state before the candidate passes.
	if (rt && r.slot != media_slot::inspect && r.slot != media_slot::project_restore) keep_hd_changes();
	switch (r.slot)
    {
    case media_slot::wav_import:
    {
        wav::Audio audio;
        wav::Result imported;
        try {
        if (!rt) err = "Start EIII before WAV import";
        else if (wav::load(r.path,audio,err)) {
            ok = wav::execute(*rt,c,audio,rq.wav_options,m_wav_cancel,imported,err);
            if (ok) {
                m_rt = std::move(imported.machine);
                c = std::move(imported.config);
                m_cfg.autoload_bank = imported.bank;
                m_cfg.autoload_preset = int(rq.wav_options.preset);
                m_cfg.autoload_drive = 1;
                r.wav_bank = imported.bank; r.wav_backup = imported.backup;
                r.wav_preset = int(rq.wav_options.preset);
            }
        }
        } catch(const std::exception& ex) {ok=false;err=ex.what();if(m_rt)m_rt->bind_thread();}
        catch(...) {ok=false;err="WAV import allocation or I/O failure";if(m_rt)m_rt->bind_thread();}
        // Worker thread only. In a DAW there is usually no captured stderr.
        // Keep a bounded, user-local failure report without changing firmware flow.
        if (!ok && !err.empty()) {
#if defined(_WIN32)
            if (const wchar_t* home=_wgetenv(L"LOCALAPPDATA")) {
                std::error_code ec;
                const auto dir=std::filesystem::path(home)/"125A"/"Emulated3";
                std::filesystem::create_directories(dir,ec);
                if (!ec) {
                    if (FILE* log=_wfopen((dir/"WAV-Import-Fehler.txt").c_str(),L"wb")) {
                        const auto image=eiii::shim::utf8_from_path(eiii::shim::path_from_utf8(c.hd_path).filename());
                        std::fprintf(log,"WAV IMPORT FAILED: %s\\nD1 image: %s\\nWAV duration: %.3f s; channels=%u; root=%u low=%u high=%u preset=%u\\n",
                            err.c_str(),image.c_str(),audio.duration(),unsigned(audio.channels),
                            unsigned(rq.wav_options.root),unsigned(rq.wav_options.low),
                            unsigned(rq.wav_options.high),unsigned(rq.wav_options.preset));
                        std::fclose(log);
                    }
                }
            }
#endif
        }
        // The host also mutes/drops MIDI during decode errors and cancellation,
        // not only during successful file loads. Always release old notes.
        try { if (m_rt) {
            // Failed jobs may have created a safety bank. Preserve it too.
            keep_hd_changes();
            publish_hd_changes(true);
            m_rt->bind_thread();
            m_rt->set_adc_source([this](double t,int16_t lr[2]){adc_sample(t,lr);});
            m_emu_frames = uint64_t(m_rt->seconds()*m_cfg.rt.sample_rate);
            m_ring_to_emu = int64_t(m_emu_frames)-int64_t(m_ring.write_index());
            midi_record pending;
            while (m_midi.pop(pending)) {} // pre-import note-ons must not follow panic
            // Keys may have been released by the host while the candidate ran.
            for(unsigned channel=0;channel<16;++channel) {
                const uint8_t panic[6]={uint8_t(0xb0|channel),64,0,uint8_t(0xb0|channel),123,0};
                m_rt->midi_in(panic,6);
            }
        }} catch(const std::exception& ex) {ok=false;err=ex.what();}
        catch(...) {ok=false;err="WAV import cleanup failed";}
        break;
    }
    case media_slot::converted_cd:
	{
		if (!rt) { err = "start EIII before importing a bank disc"; break; }
		std::error_code ec;
		auto const cache = eiii::shim::utf8_from_path(std::filesystem::temp_directory_path(ec) / "125A-Emulated3-BankCache");
		if (ec) { err = "cannot determine bank cache directory"; break; }
		ok = rt->mount_converted_bank_cd(r.path, cache, err);
		if (ok) {
			c.cd_path = bank_import::cached_path(r.path, cache);
			r.path = c.cd_path;
		}
		break;
	}
	case media_slot::hd_changes:
		ok = r.scsi_id == 1 || (r.scsi_id < 8 && scsi_extra_id(r.scsi_id) && c.scsi[r.scsi_id].kind == scsi_kind::hd);
		if (!ok) err = "no hard disk at SCSI " + std::to_string(r.scsi_id);
		else { c.hd_changes[r.scsi_id] = r.path; restart = true; }
		break;
	case media_slot::rom:
		ok = runtime::check_rom_dir(r.path, err);
		if (ok) { c.rom_dir = r.path; restart = true; }
		break;
	case media_slot::cem_model:
		ok = r.path == "v1" || r.path == "v2";
		if (!ok) err = "analog model must be v1 or v2";
		else { c.cem_model = r.path; restart = true; }
		break;
	case media_slot::project_restore:
	{
		// A host may deliver setState() after setActive(true). Prepare a
		// completely independent EIII before touching the running machine.
		// The existing machine contains unsaved volatile sample/bank RAM.
		engine_config incoming=rq.project_config;
		incoming.first_start_midi_omni=false;
		ok=incoming.rt.sample_rate==c.sample_rate &&
		   (incoming.rt.cem_model=="v1" || incoming.rt.cem_model=="v2");
		if (!ok) err="incompatible project engine configuration";
		if (!ok) break;
		if(!rq.preparation_done.load(std::memory_order_acquire) || !rq.preparation_ok) {
			ok=false;err=rq.preparation_error.empty()?"project validation failed":rq.preparation_error;break;
		}
		if ((!live || !m_power_state) && !rq.project_snapshot.empty()) {
			ok=false; err="machine snapshot requires a powered candidate restore";
			break;
		}
		if (!live || !m_power_state || (!rt && !rq.prepared_project && rq.project_snapshot.empty())) {
			// Powered-off machines have no live RAM to preserve. The pending
			// project will be loaded on their next ordinary power-on.
			// Configuration and its published disk deltas are one transaction even
			// while POWER is off. Never resave the preceding project's cached HDD.
			std::array<std::string,8> changes;
			for(unsigned id=0;id<8;++id)changes[id]=incoming.rt.hd_changes[id];
			{
				std::lock_guard<std::mutex> lock(m_project_mutex);
				if(r.project_generation && r.project_generation!=m_project_generation) {
					ok=false;err="superseded project restore";break;
				}
				m_cfg=std::move(incoming);
				m_configuration_generation=m_committed_project_generation=r.project_generation;
			}
			if (rq.on_project_commit) rq.on_project_commit();
			{
				std::lock_guard<std::mutex> lock(m_hd_mutex);
				for(unsigned id=0;id<8;++id)if(m_hd_pub[id]!=changes[id]) {
					m_hd_pub[id].swap(changes[id]);++m_hd_serial;
				}
			}
			break;
		}
		std::unique_ptr<runtime> candidate;
		std::array<std::string,8> committed_changes;
		std::array<uint64_t,8> committed_writes{};
		std::string committed_warning;
		try {
			ok=rq.preparation_done.load(std::memory_order_acquire) &&
			   rq.preparation_ok && bool(rq.prepared_project);
			if(!ok)err=rq.preparation_error.empty()?"project candidate not prepared":rq.preparation_error;
			candidate=std::move(rq.prepared_project);
			if(ok) {
				// All allocating publication data must exist before the swap.
				for(unsigned id=1;id<8;++id)if(candidate->drive_kind(id)==scsi_kind::hd) {
					committed_writes[id]=candidate->hd_writes(id);
					if(candidate->hd_changed_sectors(id))committed_changes[id]=candidate->hd_changes(id);
				}
				committed_warning=candidate->open_warning();
			}

			std::lock_guard<std::mutex> commit_lock(m_project_mutex);
			if(ok && r.project_generation && r.project_generation!=m_project_generation) {
				ok=false;err="superseded project restore";
			}
			if(ok) {
				candidate->set_adc_source([this](double t,int16_t lr[2]) { adc_sample(t,lr); });
				// Commit only after firmware and all overlays have passed.
				m_cfg=std::move(incoming);
				m_rt.swap(candidate); // candidate now owns the old machine
				m_configuration_generation=m_committed_project_generation=r.project_generation;
				r.restarted=true;
			}
		} catch (const std::exception& ex) {
			ok=false; err=ex.what();
		} catch (...) {
			ok=false; err="candidate restore failed";
		}
		candidate.reset(); // destroys old runtime only on a successful commit
		if (m_rt) m_rt->bind_thread(); // restore MAME shim state after candidate teardown
		if (ok) {
			if (rq.on_project_commit) rq.on_project_commit();
			{
				std::lock_guard<std::mutex> lock(m_hd_mutex);
				m_warning.swap(committed_warning);
				for(unsigned id=1;id<8;++id) {
					m_hd_seen[id]=committed_writes[id];m_hd_dirty_ns[id]=0;
					if(m_hd_pub[id]!=committed_changes[id]) {
						m_hd_pub[id].swap(committed_changes[id]);++m_hd_serial;
					}
				}
			}
			m_emu_frames=m_rt->audio_frame_position();
			m_ring_to_emu=int64_t(m_emu_frames)-int64_t(m_ring.write_index());
			try {publish(true,"");}
			catch(...) {
				m_rt->snapshot(m_work);m_work.running=true;m_work.status[0]=0;
				m_snap.back()=m_work;m_snap.publish();
			}
		} else err="Project restore rejected: "+err;
		break;
	}
	case media_slot::floppy:
		// Native hot insertion: do not reboot or touch SCSI/HD state.
		// Validate and load before changing the currently mounted image.
		if(r.path.empty()) {if(rt)rt->eject_floppy();c.floppy_path.clear();ok=true;}
		else if(rt) {if((ok=rt->mount_floppy(r.path,err)))c.floppy_path=r.path;}
		else if((ok=runtime::check_floppy_image(r.path,err)))c.floppy_path=r.path;
		break;
	case media_slot::restart:
		ok = true; restart = true;
		break;
	case media_slot::inspect:
		ok = rt != nullptr;
		if (ok && rq.fn) rq.fn(*rt);
		if (!ok) err = "no machine running";
		break;
	case media_slot::scsi:
	{
		unsigned const id = r.scsi_id;
		if (id == 1)
		{
			if (r.kind != scsi_kind::hd || r.path.empty()) err = "SCSI 1 is the OS hard disk: choose a hard disk image";
			else if ((ok = runtime::check_hd_image(r.path, err))) { if (r.path != c.hd_path) c.hd_changes[1].clear(); c.hd_path = r.path; restart = true; }
		}
		else if (id == 3)
		{
			if (r.kind != scsi_kind::cd) err = "SCSI 3 is the unit's CD-ROM drive";
			else if (r.path.empty()) { if (rt) rt->eject_cd(3); c.cd_path.clear(); ok = true; }
			else if (rt) { if ((ok = rt->mount_cd(3, r.path, err))) c.cd_path = r.path; }
			else if ((ok = runtime::check_cd_image(r.path, err))) c.cd_path = r.path;
		}
		else if (id < 8 && scsi_extra_id(id))
		{
			scsi_target &t = c.scsi[id];
			bool const hot = r.kind == scsi_kind::cd && t.kind == scsi_kind::cd && rt && rt->drive_kind(id) == scsi_kind::cd;
			if (r.kind == scsi_kind::hd) ok = !r.path.empty() && runtime::check_hd_image(r.path, err);
			else if (r.kind == scsi_kind::cd) ok = r.path.empty() || runtime::check_cd_image(r.path, err);
			else ok = true;
			if (r.kind == scsi_kind::hd && r.path.empty()) err = "choose a hard disk image";
			if (ok && hot) ok = r.path.empty() ? rt->eject_cd(id) : rt->mount_cd(id, r.path, err);
			if (ok)
			{
				restart = !hot && !(r.kind == t.kind && r.path == t.path && r.kind == scsi_kind::none);
				if (r.kind != t.kind || r.path != t.path) c.hd_changes[id].clear();   // another disk: its own changes
				t = { r.kind, r.kind == scsi_kind::none ? std::string() : r.path };
			}
		}
		else err = "SCSI ID must be 1, 3 or 4..7 (the OS does not use D2)";
		break;
	}
	default: err = "unsupported request"; break;
	}
	r.ok = ok;
	r.error = ok ? "" : err;
	if (r.slot==media_slot::project_restore) {
		r.ok=ok;
		r.error=ok ? "" : err;
		return; // already prepared and committed, or old runtime untouched
	}
	if (!ok || !restart || !live || !m_power_state) return;
	// Non-project user media changes retain their existing cold-boot behavior.
	m_rt.reset();
	std::string berr;
	const bool booted=boot(berr);
	if (!booted) { r.ok=false; r.error=berr; }
	publish(m_rt != nullptr,berr.c_str());
	r.restarted=true;
	if (m_rt && r.slot==media_slot::hd_changes && !m_rt->open_warning().empty()) {
		r.ok=false; r.error=m_rt->open_warning();
	}
}

int engine::initial_restore_result(std::string& error) const
{
 int result=m_initial_restore_status.load(std::memory_order_acquire);
 if(result==3) {std::lock_guard<std::mutex> lock(m_hd_mutex);error=m_initial_restore_error;}
 return result;
}

void engine::start(engine_config const &cfg, std::vector<uint8_t> initial_snapshot, uint64_t generation)
{
	shutdown();
	m_initial_snapshot=std::move(initial_snapshot);
	m_initial_generation=generation;
	{
		std::lock_guard<std::mutex> lock(m_project_mutex);
		m_project_generation=generation;
		m_committed_project_generation=0;m_configuration_generation=generation;
	}
	{
		std::lock_guard<std::mutex> lock(m_hd_mutex);m_initial_restore_error.clear();
	}
	m_initial_restore_status.store(m_initial_snapshot.empty()?0:1,std::memory_order_release);
	m_cfg = cfg;
	m_sample_rate=cfg.rt.sample_rate;
	m_realtime = cfg.realtime;
	m_latency = latency_for(cfg.rt.sample_rate, cfg.max_block, m_ring.capacity());
	m_quit = false;
	m_primed = false;
	m_view = m_cfg.rt;
	{
		std::lock_guard<std::mutex> g(m_hd_mutex);
		for (unsigned id = 0; id < 8; ++id) m_hd_pub[id] = m_cfg.rt.hd_changes[id];
		++m_hd_serial;
	}
	m_power_state = false;
	m_last_pull_ns = 0;
	m_thread = std::thread([this] {
        const auto fail = [this]() noexcept {
            m_quit = true;
            if(m_initial_restore_status.load(std::memory_order_acquire)==1)
                m_initial_restore_status.store(3,std::memory_order_release);
            audio_progress();
            m_wav_busy.store(false);
            m_primed = false;
            panel_snapshot snapshot{};
            snapshot.running = false;
            std::snprintf(snapshot.status, sizeof(snapshot.status),
                          "EIII engine error; restart instrument");
            m_snap.back() = snapshot;
            m_snap.publish();
        };
        try { thread_main(); }
        catch (const std::exception &ex) {
            fail();
            std::fprintf(stderr, "EIII engine failure: %s\n", ex.what());
        }
        catch (...) { fail(); }
    });
}

void engine::shutdown()
{
    m_wav_cancel.store(true);
	m_quit = true;
	audio_progress(); // unblock an offline reader before joining the producer
	if (m_thread.joinable()) {
		m_thread.join();
	}
    m_wav_busy.store(false,std::memory_order_release);
    m_firmware_setup_busy.store(false,std::memory_order_release);
    media_request* pending=nullptr;
    while(m_media_in.pop(pending)) {
        pending->r.ok=false;pending->r.error="Request cancelled by engine shutdown";
        m_media_out.push(pending); // total outstanding requests <= 8, output capacity 16
    }
	// The worker has stopped. Flush all events/audio from the previous runtime
	// before another instance activation; no obsolete NoteOn/NoteOff may leak.
	midi_record mr{};
	while (m_midi.pop(mr)) {}
	panel_record pr{};
	while (m_panel.pop(pr)) {}
	m_ring.drain();
	m_in.drain();
	m_in_buf.clear();
	m_in_end = m_in.read_index();
}

bool engine::post(panel_input const &in)
{
	if (m_wav_busy.load()) return false;
	if (m_panel.push({ m_ring.read_index() + latency(), in })) return true;
	++m_panel_dropped; return false;
}

bool engine::snapshot(panel_snapshot &out) { return m_snap.read(out); }
void engine::power(bool on) { m_power_req = on; audio_progress(); }

engine_stats engine::stats() const
{
	engine_stats s;
	s.frames_out = m_frames_out; s.underruns = m_underruns; s.first_underrun_at = m_first_underrun_at; s.underrun_frames = m_underrun_frames; s.dropped_frames = m_dropped;
	s.midi_dropped = m_midi_dropped; s.panel_dropped = m_panel_dropped; s.emu_seconds = m_emu_seconds; s.busy_fraction = m_busy;
	s.midi_events = m_midi_events; s.midi_late_frames = m_midi_late_frames;
	s.midi_trace_hash = m_midi_trace_hash;
	return s;
}

bool engine::midi(uint32_t block_offset, uint8_t const *bytes, uint8_t n)
{
	if (m_wav_busy.load()) return false;
	uint64_t const f = m_ring.read_index() + block_offset + latency();
	bool ok = true;
	for (uint8_t i = 0; i < n; i += 3)
	{
		midi_record r{ f, uint8_t(std::min(3, n - i)), {} };
		memcpy(r.b, bytes + i, r.n);
		if (!m_midi.push(r)) { ++m_midi_dropped; ok = false; }
	}
	return ok;
}

void engine::ensure_realtime_block(size_t frames)
{
 unsigned requested=latency_for(sample_rate(),frames,m_ring.capacity());
 unsigned current=latency();
 // Never rewind already queued MIDI/input timing when the host uses a smaller
 // buffer. Growing lookahead requires no allocation or hardware restart.
 while(current<requested && !m_latency.compare_exchange_weak(current,requested)) {}
 if(current<requested)audio_progress();
}

void engine::set_realtime(bool rt)
{
	if (m_realtime.exchange(rt) != rt) audio_progress();
}

size_t engine::pull(float *left, float *right, size_t frames)
{
	m_last_pull_ns = now_ns();
	size_t got = 0;
	if (frames && !m_realtime.load()) {
		m_offline_pull.store(true, std::memory_order_release);
		audio_progress();
		while (got < frames) {
			const auto epoch = m_audio_progress.load(std::memory_order_acquire);
			if (m_quit.load() || m_realtime.load()) break;
			const size_t n = m_ring.read(left + got, right + got, frames - got);
			got += n;
			if (n) audio_progress(); // release space even for blocks exceeding latency/capacity
			else m_audio_progress.wait(epoch, std::memory_order_acquire);
		}
		m_offline_pull.store(false, std::memory_order_release);
		audio_progress();
	} else {
		got = m_ring.read(left, right, frames); // realtime/prefetch remains nonblocking
	}
	if (got < frames)
	{
		std::fill(left + got, left + frames, 0.0f); std::fill(right + got, right + frames, 0.0f);
		if (m_primed) { if (!m_underruns) m_first_underrun_at = m_frames_out.load(); ++m_underruns; m_underrun_frames += frames - got; }
	}
	if (got) m_frames_out += got; // match the ring/MIDI read clock, including partial realtime reads
	return frames;
}

bool engine::boot(std::string &err)
{
 auto initial=std::move(m_initial_snapshot); // never replay on an ordinary POWER cycle
 bool const restoring=!initial.empty();
 // Check actual image format/size before the emulator attempts to mount media.
 // The same validators are used for interactive media changes.
 if (!runtime::check_rom_dir(m_cfg.rt.rom_dir,err)) return false;
 if (m_cfg.rt.hd_path.empty() && m_cfg.rt.floppy_path.empty()) {err="no EIII OS disk: select D0 floppy or D1 hard disk";return false;}
 if (!m_cfg.rt.hd_path.empty() && !runtime::check_hd_image(m_cfg.rt.hd_path,err)) return false;
 if (!m_cfg.rt.floppy_path.empty() && !runtime::check_floppy_image(m_cfg.rt.floppy_path,err)) return false;
 if (!m_cfg.rt.cd_path.empty() && !runtime::check_cd_image(m_cfg.rt.cd_path,err)) return false;
 for(unsigned id=4;id<=7;++id) {
  const auto& drive=m_cfg.rt.scsi[id];
  if(drive.path.empty()) continue;
  if(drive.kind==scsi_kind::hd && !runtime::check_hd_image(drive.path,err)) return false;
  if(drive.kind==scsi_kind::cd && !runtime::check_cd_image(drive.path,err)) return false;
 }
	m_rt = std::make_unique<runtime>();
	const bool opened=restoring
		?m_rt->open_restored(m_cfg.rt,initial.data(),initial.size(),err)
		:m_rt->open(m_cfg.rt,err);
	if(!opened || !m_rt->media_restore_valid()) {
		if(err.empty())err=m_rt->open_warning();m_rt.reset();return false;
	}
	m_rt->set_adc_source([this](double t, int16_t lr[2]) { adc_sample(t, lr); });
	if (!m_rt->open_warning().empty()) { std::lock_guard<std::mutex> g(m_hd_mutex); m_warning = m_rt->open_warning(); }
	publish_hd_changes(true);
	m_emu_frames = restoring?m_rt->audio_frame_position():0;
	m_ring_to_emu = int64_t(m_emu_frames)-int64_t(m_ring.write_index());
	{
		std::lock_guard<std::mutex> lock(m_project_mutex);
		if(restoring && m_initial_generation!=m_project_generation) {
			err="initial project superseded";m_rt.reset();return false;
		}
		m_committed_project_generation=m_configuration_generation;
	}
	return true;
}

void engine::publish(bool running, char const *status)
{
#if defined(_WIN32)
    // Diagnostic writes run solely on the emulation worker, never in VST3 process().
    // Log only status transitions; snapshots themselves can be published every millisecond.
    static thread_local std::string last_status;
    static thread_local bool last_running = false;
    const std::string current = status ? status : "";
    if (last_running != running || last_status != current)
    {
        last_running = running;
        last_status = current;
        if (const char *path = std::getenv("EIII_DIAG_FILE"))
        {
            if (FILE *fp = std::fopen(path, "a"))
            {
                std::fprintf(fp, "EIII runtime: %s | %s\\n",
                    running ? "RUNNING" : "NOT RUNNING",
                    current.empty() ? "(no status message)" : current.c_str());
                std::fclose(fp);
            }
        }
    }
#endif
	if (m_rt) m_rt->snapshot(m_work);
	else
	{
		memset(&m_work.lcd, 0, sizeof m_work.lcd);
		m_work.led_latch = 0xffff; m_work.misc_latch = 0xff; m_work.emu_seconds = 0;
		std::fill(std::begin(m_work.adc_inputs),std::end(m_work.adc_inputs),uint16_t(0));
		m_work.adc_valid = false;
		++m_work.serial;
	}
#if defined(_WIN32)
    // Full 4x20 visible EIII LCD, including lines 3 and 4 where the
    // firmware shows selection prompts. Log changes as well as once/second.
    // Worker thread only; never called from the DAW's audio callback.
    if (running && m_rt)
    {
        static thread_local std::string last_screen;
        static thread_local int64_t last_lcd_second = -1;
        static thread_local double last_logged_at = -1.0;
        const int64_t current_second = int64_t(m_work.emu_seconds);
        std::string screen;
        for (int row = 0; row < 4; ++row) {
            if (row) screen += "|";
            const int base = row == 0 ? 0x00 : row == 1 ? 0x40 : row == 2 ? 0x14 : 0x54;
            for (int i = 0; i < 20; ++i) {
                const uint8_t ch = m_work.lcd.ddram[base+i];
                screen += ch >= 32 && ch <= 126 ? char(ch) : ' ';
            }
        }
        bool changed = screen != last_screen;
        if ((changed && (last_logged_at < 0 || m_work.emu_seconds - last_logged_at >= .02)) ||
            current_second != last_lcd_second)
        {
            if (const char *path = std::getenv("EIII_DIAG_FILE"))
                if (FILE *fp = std::fopen(path, "a")) {
                    std::fprintf(fp, "LCD4 t=%.3f display=%d rows=[%s] leds=%04X\\n",
                        m_work.emu_seconds, int(m_work.lcd.display_on),
                        screen.c_str(), unsigned(m_work.led_latch));
                    std::fclose(fp);
                }
            last_screen = screen;
            last_logged_at = m_work.emu_seconds;
            last_lcd_second = current_second;
        }
    }
#endif
	m_work.running = running && m_rt;
	snprintf(m_work.status, sizeof m_work.status, "%s", status ? status : "");
	m_snap.back() = m_work;
	m_snap.publish();
}

void engine::thread_main()
{
    // Reuse the upstream CLAP front-panel macro: the original firmware handles
    // loading; no direct writes to emulated memory or any realtime callback.
    ui_macro autoload;
    bool autoload_pending = false;
    auto init_autoload = [&] {
        autoload_pending = false;
        m_firmware_setup_busy.store(false,std::memory_order_release);
        if (!m_rt || (m_cfg.autoload_bank < 0 && !m_cfg.first_start_midi_omni)) return;
        autoload = ui_macro{};
        autoload.panel = [this](std::string const &name, bool down) {
            if (!m_rt) return;
            for (size_t i = 0; i < panel_control_count; ++i) {
                auto const &k = panel_controls[i];
                if (name != k.internal) continue;
#if defined(_WIN32)
                if (const char *path = std::getenv("EIII_DIAG_FILE"))
                    if (FILE *fp = std::fopen(path, "a")) {
                        std::fprintf(fp, "AUTOLOAD INPUT t=%.3f %s %s code=%02X route=%s\\n",
                            m_rt->seconds(), down ? "DOWN" : "UP", name.c_str(),
                            unsigned(k.code), k.route == panel_route::scan ? "SCAN" :
                            k.route == panel_route::xsw ? "XSW" : "OTHER");
                        std::fclose(fp);
                    }
#endif
                if (k.route == panel_route::scan)
                    m_rt->panel_switch(k.code, down);
                else if (k.route == panel_route::xsw)
                    m_rt->extra_switch(k.code, down);
                return;
            }
#if defined(_WIN32)
            if (const char *path = std::getenv("EIII_DIAG_FILE"))
                if (FILE *fp = std::fopen(path, "a")) {
                    std::fprintf(fp, "AUTOLOAD INPUT UNKNOWN control=%s\\n", name.c_str());
                    std::fclose(fp);
                }
#endif
        };
        autoload.extra = autoload.panel;
        autoload.data_slider = [this](uint16_t adc) {
            if (m_rt) m_rt->set_adc_input(3, adc); // X3: original hardware DATA pot
        };
        autoload.lcd = [this] {
            if (!m_rt) return std::string{};
            panel_snapshot snapshot{};
            m_rt->snapshot(snapshot);
            return firmware_menu_lcd(snapshot.lcd);
        };
        autoload.m_log = [](double when, std::string const &message) {
#if defined(_WIN32)
            if (const char *path = std::getenv("EIII_DIAG_FILE"))
                if (FILE *fp = std::fopen(path, "a")) {
                    std::fprintf(fp, "AUTOLOAD t=%.2f %s\n", when, message.c_str());
                    std::fclose(fp);
                }
#endif
        };
        auto const ready = "^P\\d\\d .*\nS\\d\\d ";
        auto const key = [](int n) { return "Keypad " + std::to_string(n); };
        autoload.wait(ready, 60, "main screen");
        if (m_cfg.first_start_midi_omni) {
            append_first_start_midi(autoload, ready);
            // One first-start attempt, not an unconditional reset at every
            // later POWER/media restart of this same machine.
            m_cfg.first_start_midi_omni = false;
        }
        if (m_cfg.autoload_bank < 0) {
            autoload_pending = true;
            m_firmware_setup_busy.store(true,std::memory_order_release);
            return;
        }
        int const bank = m_cfg.autoload_bank, drive = m_cfg.autoload_drive;
        char bank_name[16]; std::snprintf(bank_name, sizeof bank_name, "B%02d ", bank);
        autoload.press("Load Bank");
        autoload.wait("LOAD BANK from", 10, "LOAD BANK menu");
        if (drive == 1)
        {
            // If D1 already contains a bank, OS 2.42 immediately opens
            // "Select a Bank". ENTER at this point LOADS the bank; it
            // does not enter "Select a Drive" (verified on UST D1 B00).
            autoload.wait("Select a Bank", 30, "bank selection on D1");
        }
        else
        {
            // The upstream CD case starts at "Select a Drive" when the
            // currently selected disk has no banks.
            autoload.extra_press("Enter");
            autoload.wait("Select a Drive", 10, "drive selection");
            autoload.press(key(drive));
            autoload.wait("\nD" + std::to_string(drive) + " ", 10, "drive");
            autoload.extra_press("Enter");
            autoload.wait("Select a Bank", 30, "bank selection");
        }
        autoload.press(key(bank / 10));
        autoload.press(key(bank % 10));
        autoload.wait(bank_name, 10, "bank number");
        autoload.extra_press("Enter");
        autoload.wait("Loading Bank", 10, "bank loading");
        autoload.wait(ready, 120, "main screen with loaded bank");
        if (m_cfg.autoload_preset >= 0) {
            const int preset = m_cfg.autoload_preset;
            char preset_name[16]; std::snprintf(preset_name, sizeof preset_name, "^P%02d ", preset);
            autoload.press(key(preset / 10));
            autoload.press(key(preset % 10));
            autoload.wait(preset_name, 10, "preset selected");
        }
        autoload_pending = true;
        m_firmware_setup_busy.store(true,std::memory_order_release);
    };
	set_ftz();
	{
		// a modest realtime priority when the process is allowed one (RLIMIT_RTPRIO / rtkit-granted hosts); else unchanged
		#if defined(_WIN32)
		SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
#else
		sched_param sp{}; sp.sched_priority = 10;
		pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp);
#endif
	}
	m_silence.assign(4096, 0.0f);
	m_in_buf.clear(); m_in_end = m_in.read_index();
	std::string status;
	std::array<std::unique_ptr<media_request>,8> pending_media;
	size_t pending_count=0;
	auto t_window = now_ns(); int64_t busy_ns = 0;
	int64_t free_t0 = 0; uint64_t free_f0 = 0;
	double const sr = m_cfg.rt.sample_rate;
	uint64_t const step_frames = std::max<uint64_t>(1, uint64_t(sr / 1000.0));
#if defined(_WIN32)
    // Persist diagnostics across one-millisecond engine iterations;
    // declaring these inside the loop produced ~1000 log writes per second.
    float audioWindowPeak = 0.0f;
    double audioLastLog = 0.0;
#endif
	while (!m_quit)
	{
		// Host project save asks for an immediate overlay flush, bypassing
		// the normal 300 ms write-coalescing delay. Only this thread accesses m_rt.
		const uint64_t requested=m_hd_refresh_req.load(std::memory_order_acquire);
		if (requested != m_hd_refresh_done.load(std::memory_order_relaxed)) {
			if (m_rt) publish_hd_changes(true);
			m_hd_refresh_done.store(requested, std::memory_order_release);
		}
		bool const want = m_power_req.load();
		if (want != m_power_state)
		{
			m_power_state = want;
			if(want) {
				const bool initial=!m_initial_snapshot.empty();
				std::string err;const bool ok=boot(err);status=ok?"":err;
				if(initial) {
					{std::lock_guard<std::mutex> lock(m_hd_mutex);m_initial_restore_error=err;}
					m_initial_restore_status.store(ok?2:3,std::memory_order_release);
					autoload_pending=false;m_firmware_setup_busy.store(false,std::memory_order_release);
				} else init_autoload();
			}
			else { keep_hd_changes(); m_rt.reset(); status = "POWER OFF"; }
			publish(m_rt != nullptr, status.c_str());
		}
		media_request *mq;
		while (pending_count<pending_media.size() && m_media_in.pop(mq)) {
			// Inspection captures the currently committed machine. It need not
			// wait behind detached preparation (which cannot yet publish state).
			// Processor getState refuses to mix pending foreign project metadata
			// with this live machine; a superseding legacy state can save again.
			if(mq->r.slot==media_slot::inspect && pending_count &&
			   pending_media[0]->r.slot==media_slot::project_restore &&
			   !pending_media[0]->preparation_done.load(std::memory_order_acquire)) {
				try {apply_media(*mq,true);}
				catch(...) {mq->r.ok=false;mq->r.error="inspection failed";}
				m_media_out.push(mq);
			} else pending_media[pending_count++].reset(mq);
		}
		while(pending_count) {
			mq=pending_media[0].get();
			if(mq->r.slot==media_slot::project_restore &&
			   !mq->preparation_done.load(std::memory_order_acquire)) {
				if(!mq->preparer.joinable()) {
					bool stale=false;
					{
						std::lock_guard<std::mutex> lock(m_project_mutex);
						stale=mq->r.project_generation && mq->r.project_generation!=m_project_generation;
					}
					try {
						if(stale) {
							mq->preparation_error="superseded project restore";
							mq->preparation_done.store(true,std::memory_order_release);
						} else {
							m_project_preparing.store(true,std::memory_order_release);
							mq->preparer=std::thread([this,mq] {
							try {set_ftz();prepare_project(*mq);}
							catch(...) {mq->preparation_ok=false;mq->preparation_error.clear();}
							m_project_preparing.store(false,std::memory_order_release);
							mq->preparation_done.store(true,std::memory_order_release);
							audio_progress();
							});
						}
					} catch(...) {
						m_project_preparing.store(false,std::memory_order_release);
						mq->preparation_error="project preparation thread unavailable";
						mq->preparation_done.store(true,std::memory_order_release);
					}
				}
				// Continue running the unchanged current EIII while another thread
				// hashes files, allocates and validates the independent candidate.
				if(!mq->preparation_done.load(std::memory_order_acquire))break;
			}
			// The preparer publishes done after its final media/device access.
			// No join from process(), nor any file/validation work after this flag.
			if(mq->preparer.joinable())mq->preparer.join();
			try {
				if (mq->r.slot==media_slot::wav_import && autoload_pending) {
					mq->r.ok=false;mq->r.error="Wait for firmware bank recall to finish before WAV import";
				} else apply_media(*mq,true);
			} catch(...) {
				mq->r.ok=false;mq->r.error="Engine worker request failed";
				pending_media[0].release();m_media_out.push(mq);
				throw;
			}
			if(mq->r.slot==media_slot::project_restore && mq->r.ok && mq->r.restarted) {
				autoload_pending=false;
				m_firmware_setup_busy.store(false,std::memory_order_release);
				if(mq->project_snapshot.empty())init_autoload();
			}
			if(mq->r.restarted || mq->r.slot==media_slot::wav_import) {status=mq->r.error;free_t0=0;}
			const bool was_wav=mq->r.slot==media_slot::wav_import;
			pending_media[0].release();
			for(size_t i=1;i<pending_count;++i)pending_media[i-1]=std::move(pending_media[i]);
			--pending_count;
			m_media_out.push(mq); // outstanding <= 8, output capacity 16
			if(was_wav)m_wav_busy.store(false,std::memory_order_release);
		}

		if(!m_rt && pending_count && pending_media[0]->r.slot==media_slot::project_restore &&
		   !pending_media[0]->project_snapshot.empty() && !m_realtime.load()) {
			auto epoch=m_audio_progress.load(std::memory_order_acquire);
			if(!pending_media[0]->preparation_done.load(std::memory_order_acquire) && !m_quit.load())
				m_audio_progress.wait(epoch,std::memory_order_acquire);
			continue;
		}

		drain_input();
		panel_record pr;
		if (!m_rt) while (m_panel.pop(pr)) { }       // no machine: switches do nothing

		int64_t const t0 = now_ns();
		const auto audio_epoch = m_audio_progress.load(std::memory_order_acquire);
		uint64_t const R = m_ring.read_index(), W = m_ring.write_index();
		// A newly restored offline instance must finish the original firmware
		// recall before its host timeline/MIDI can start. Run the unchanged menu
		// macro at emulated speed, discarding only boot audio. No CPU/ADC clock
		// or menu shortcut is introduced. A warm mode switch never enters here.
		const bool preparing = m_rt && !m_realtime.load() && autoload_pending;
		bool const consumer = m_offline_pull.load(std::memory_order_acquire) ||
		    (m_last_pull_ns.load() != 0 && t0 - m_last_pull_ns.load() < 200'000'000);
		uint64_t target_ring;
		if (preparing) {
			free_t0 = 0;
			target_ring = W + step_frames;
		}
		else if (consumer)
		{
			free_t0 = 0;
			uint64_t const want_w = R + latency();
			if (W >= want_w) {
				m_primed = true;
				if (m_offline_pull.load(std::memory_order_acquire) && !m_realtime.load()) {
					if (!m_quit.load()) m_audio_progress.wait(audio_epoch, std::memory_order_acquire);
				} else std::this_thread::sleep_for(std::chrono::microseconds(500));
				continue;
			}
			target_ring = std::min(want_w, W + step_frames);
		}
		else
		{
			// nobody pulls audio (host stopped processing, or GUI-only use): run on the wall clock so the panel stays
			// alive; keep at most `latency` frames queued and drop the rest
			if (!m_rt) { std::this_thread::sleep_for(std::chrono::milliseconds(2)); continue; }
			if (!free_t0) { free_t0 = t0; free_f0 = m_emu_frames; }
			uint64_t const due = free_f0 + uint64_t(double(t0 - free_t0) * 1e-9 * sr);
			if (due <= m_emu_frames) { std::this_thread::sleep_for(std::chrono::milliseconds(1)); continue; }
			target_ring = uint64_t(int64_t(std::min(due, m_emu_frames + step_frames)) - m_ring_to_emu);
		}

		if (!m_rt)
		{
			// powered off / boot error: keep the host fed with silence at the same latency
			size_t const n = size_t(std::min<uint64_t>(target_ring - W, m_silence.size() / 2));
			m_ring.write(m_silence.data(), n);
			audio_progress();
			continue;
		}

		auto push = [&] {
			m_rt->render_audio();
			auto &a = m_rt->audio();
			size_t const n = a.size() / 2;
#if defined(_WIN32)
            if (std::getenv("EIII_DIAG_FILE")) {
                // Firmware can reboot without restarting the engine worker.
                if (m_rt->seconds() < audioLastLog) {
                    audioLastLog = 0.0;
                    audioWindowPeak = 0.0f;
                }
                for (float v : a) audioWindowPeak = std::max(audioWindowPeak, std::fabs(v));
                if (m_rt->seconds() >= audioLastLog + 2.0) {
                    if (const char *path = std::getenv("EIII_DIAG_FILE"))
                        if (FILE *fp = std::fopen(path, "a")) {
                            std::fprintf(fp, "AUDIO ENGINE t=%.2f 2s_peak=%g ring_fill=%zu underruns=%llu midi_dropped=%llu\n",
                                m_rt->seconds(), double(audioWindowPeak), m_ring.fill(),
                                (unsigned long long)m_underruns.load(),
                                (unsigned long long)m_midi_dropped.load());
                            std::fclose(fp);
                        }
                    audioLastLog = m_rt->seconds();
                    audioWindowPeak = 0.0f;
                }
            }
#endif
			size_t const w = !preparing && (consumer || m_ring.fill() < latency()) ? m_ring.write(a.data(), n) : 0;
			if (w) audio_progress();
			if (w < n) { m_dropped += n - w; m_ring_to_emu += int64_t(n - w); }
			m_emu_frames += n;
			a.clear();
		};
		auto cycle_of = [&](int64_t emu_frame) { return uint64_t(std::ceil(double(emu_frame) * runtime::cpu_hz / sr)); };

		int64_t const target_emu = int64_t(target_ring) + m_ring_to_emu;
		// timestamped inputs (MIDI from the audio thread, panel from the GUI thread), earliest first, each applied at its
		// own frame
		for (;;)
		{
			midi_record mr; bool const hm = !preparing && m_midi.peek(mr);
			bool const hp = m_panel.peek(pr);
			if (!hm && !hp) break;
			bool const use_midi = hm && (!hp || mr.ring_frame <= pr.ring_frame);
			int64_t const f = int64_t(use_midi ? mr.ring_frame : pr.ring_frame) + m_ring_to_emu;
			if (f > target_emu) break;
			if (f > int64_t(m_emu_frames)) { m_rt->run_until_cycle(cycle_of(f)); push(); }
			if (use_midi)
            {
#if defined(_WIN32)
                // Worker-side UART handoff diagnostics (never the audio callback).
                if (mr.n >= 3 && ((mr.b[0] & 0xf0) == 0x80 || (mr.b[0] & 0xf0) == 0x90))
                    if (const char *path = std::getenv("EIII_DIAG_FILE"))
                        if (FILE *fp = std::fopen(path, "a")) {
                            std::fprintf(fp, "MIDI UART QUEUED t=%.3f status=%02X note=%u velocity=%u\n",
                                m_rt->seconds(), unsigned(mr.b[0]), unsigned(mr.b[1]), unsigned(mr.b[2]));
                            std::fclose(fp);
                        }
#endif
                m_rt->midi_in(mr.b, mr.n);
                // Diagnostic fingerprint of the actual UART handoff, including
                // its absolute host-frame timestamp and every transmitted byte.
                uint64_t hash = m_midi_trace_hash.load();
                auto byte = [&](uint8_t b) { hash = (hash ^ b) * 1099511628211ull; };
                for (unsigned k=0;k<8;++k) byte(uint8_t(mr.ring_frame >> (8*k)));
                byte(mr.n);
                for (unsigned k=0;k<mr.n;++k) byte(mr.b[k]);
                m_midi_trace_hash.store(hash);
                ++m_midi_events;
                if (int64_t(m_emu_frames) > f)
                    m_midi_late_frames.store(std::max(m_midi_late_frames.load(), uint64_t(int64_t(m_emu_frames)-f)));
                m_midi.pop(mr);
            }
			else
			{
				panel_input const &in = pr.in;
				if (in.kind == panel_input::SCAN) m_rt->panel_switch(in.code, in.down);
				else if (in.kind == panel_input::XSW) m_rt->extra_switch(in.code, in.down);
				else if (in.kind == panel_input::ADC) m_rt->set_adc_input(in.code, in.value);
				m_panel.pop(pr);
			}
		}
		m_rt->run_until_cycle(cycle_of(target_emu));
		push();
		if (m_rt->stop_code() == 2) { status = "emulation stopped (watchdog)"; keep_hd_changes(); m_rt.reset(); }
		else publish_hd_changes(false);
        if (autoload_pending && m_rt) {
            autoload.tick(m_rt->seconds());
            if (autoload.failed() || autoload.done()) {
                if (autoload.failed()) {
                    status = std::string("Firmware panel setup failed: ") + autoload.error();
                    autoload.m_log(m_rt->seconds(),
                        std::string("FAILED: ") + autoload.error());
                } else autoload.m_log(m_rt->seconds(), "COMPLETE");
                autoload_pending = false;
                m_firmware_setup_busy.store(false,std::memory_order_release);
            }
        }
		publish(m_rt != nullptr, status.c_str());
		m_emu_seconds = m_rt ? m_rt->seconds() : 0.0;

		int64_t const t1 = now_ns();
		busy_ns += t1 - t0;
		if (t1 - t_window > 500'000'000) { m_busy = double(busy_ns) / double(t1 - t_window); busy_ns = 0; t_window = t1; }
	}
	for(size_t i=0;i<pending_count;++i) {
		auto* request=pending_media[i].get();
		if(request->preparer.joinable())request->preparer.join();
		request->prepared_project.reset();
		request->r.ok=false;request->r.error="engine stopped";
		m_media_out.push(pending_media[i].release());
	}
	if(m_rt)m_rt->bind_thread();
	keep_hd_changes();
	m_rt.reset();
	// the final change sets, for a host that starts the engine again (e.g. at another sample rate)
	std::lock_guard<std::mutex> g(m_hd_mutex);
	for (unsigned id = 1; id < 8; ++id) if (m_hd_pub[id] != m_cfg.rt.hd_changes[id]) { m_hd_pub[id] = m_cfg.rt.hd_changes[id]; ++m_hd_serial; }
}

} // namespace eiii
