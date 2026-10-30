// Runtime for the shim: scheduler, globals, image devices.
#include "emu.h"
#include "sector_bounds.h"
#include "large_file_io.h"
#include "utf8_file_paths.h"
#include "image_fingerprint.h"
#include "imagedev/harddriv.h"
#include "imagedev/cdromimg.h"
#include "sound/cdda.h"
#include <algorithm>
#include <cstring>
#include <fcntl.h>
#include <stdexcept>

running_machine device_t::s_machine;
thread_local FILE *device_t::s_log = nullptr;
thread_local address_space *device_memory_interface::s_host_space = nullptr;
thread_local void (*g_insn_hook)(offs_t) = nullptr;
void shim_fatal(std::string const &msg) { throw std::runtime_error("EIII emulator fatal: " + msg); }

static shim_scheduler s_default_sched;
thread_local shim_scheduler *g_shim_sched = &s_default_sched;
shim_scheduler *shim_default_scheduler() { return &s_default_sched; }

emu_timer::~emu_timer()
{
	auto &v = m_sched->m_timers;
	v.erase(std::remove(v.begin(), v.end(), this), v.end());
}

attotime shim_scheduler::time() const
{
	if (m_cpu_running && m_cpu) return m_cpu->local_time();
	return m_basetime;
}

bool shim_scheduler::capture_recall_state(recall_state &out) const
{
	if (m_cpu_running) return false;
	recall_state tmp;
	tmp.basetime = m_basetime;
	tmp.timers.reserve(m_timers.size());
	for (emu_timer const *t : m_timers)
	{
		if (!t) return false;
		tmp.timers.push_back({t->m_expire, t->m_start, t->m_period,
			t->m_param, t->m_enabled});
	}
	out = std::move(tmp);
	return true;
}

bool shim_scheduler::valid_recall_state(recall_state const &state) const
{
	// Validate the entire snapshot before modifying scheduler or any timer.
	if (m_cpu_running || state.basetime.is_never() ||
		state.timers.size() != m_timers.size()) return false;
	for (size_t i = 0; i < state.timers.size(); ++i)
	{
		auto const &t = state.timers[i];
		if (!m_timers[i]) return false;
		for (auto time : {t.expire, t.start, t.period})
			if (!std::isfinite(time.m_seconds) || time.m_seconds < 0 ||
			    time.m_seconds > attotime::never.m_seconds) return false;
	}
	return std::isfinite(state.basetime.m_seconds) && state.basetime.m_seconds >= 0;
}

bool shim_scheduler::restore_recall_state(recall_state const &state)
{
	if (!valid_recall_state(state)) return false;
	m_basetime = state.basetime;
	m_cpu_slice_start = state.basetime;
	for (size_t i = 0; i < state.timers.size(); ++i)
	{
		auto *dest = m_timers[i];
		auto const &src = state.timers[i];
		dest->m_expire = src.expire;
		dest->m_start = src.start;
		dest->m_period = src.period;
		dest->m_param = src.param;
		dest->m_enabled = src.enabled;
	}
	return true;
}

attotime shim_scheduler::next_expiry() const
{
	attotime n = attotime::never;
	for (emu_timer const *t : m_timers)
		if (t->m_enabled && t->m_expire < n) n = t->m_expire;
	return n;
}

void shim_scheduler::fire_due(attotime upto)
{
	// fire in expiry order, each at its own time (MAME executes timers in time order)
	for (;;)
	{
		emu_timer *best = nullptr;
		for (emu_timer *t : m_timers)
			if (t->m_enabled && t->m_expire <= upto && (!best || t->m_expire < best->m_expire)) best = t;
		if (!best) break;
		m_basetime = best->m_expire;
		if (!best->m_period.is_never() && !best->m_period.is_zero()) best->m_expire = best->m_expire + best->m_period;
		else best->m_enabled = false;
		best->m_cb(best->m_param);
	}
	if (m_basetime < upto) m_basetime = upto;
}

emu_timer *shim_scheduler::timer_alloc(timer_expired_delegate d)
{
	m_owned.emplace_back(new emu_timer(std::move(d.m_f)));
	return m_owned.back().get();
}

void shim_scheduler::abort_timeslice()
{
	if (m_cpu_running && m_cpu) m_cpu->abort_timeslice();
}

// ---- hard disk (read-only file + RAM overlay) ----
harddisk_image_device::harddisk_image_device(machine_config const &mc, char const *tag, device_t *owner, u32 clock)
	: device_t(mc, cdda_device::s_type, tag, owner, clock) { }

bool harddisk_image_device::load(std::string const &path)
{
	m_f = eiii::shim::fopen_utf8(path, "rb");
	if (!m_f) return false;
	if (!eiii::shim::file_size(m_f, m_length)) { fclose(m_f); m_f=nullptr; return false; }
	// ask the kernel to start reading the OS image into the page cache now (asynchronously), so the boot's SCSI reads
	// do not wait for the disk while the engine thread must stay ahead of the audio
	#if !defined(_WIN32)
	posix_fadvise(fileno(m_f), 0, 0, POSIX_FADV_WILLNEED);
#endif
	{
		// Run on the emulator owner/loader thread, never in the audio callback.
		// Stream the entire read-only image once to bind future overlays to all
		// base sectors, not just the first 64 KiB boot/system header.
		u8 buf[65536];
		if (!eiii::shim::seek(m_f, 0)) { fclose(m_f); m_f=nullptr; return false; }
		eiii::shim::image_fingerprint hashes;
		for (;;) {
			const size_t n=fread(buf, 1, sizeof buf, m_f);
			if (n) hashes.update(buf,n);
			if (n != sizeof buf) break;
		}
		if (ferror(m_f) || hashes.seen != m_length) {
			fclose(m_f); m_f=nullptr; return false;
		}
		m_fingerprint = hashes.prefix;
		m_fingerprint_full = hashes.full;
	}
	m_overlay.clear(); m_writes = 0; m_legacy_overlay = false;
	// MAME lib/util/harddisk.cpp geometry guess (raw image, skip 0)
	m_info = {};
	m_info.sectorbytes = 512;
	for (u32 totalsectors = u32(m_length / m_info.sectorbytes); ; totalsectors++)
		for (u32 cursectors = 63; cursectors > 1; cursectors--)
			if (totalsectors % cursectors == 0)
			{
				u32 const totalheads = totalsectors / cursectors;
				for (u32 curheads = 16; curheads > 1; curheads--)
					if (totalheads % curheads == 0)
					{
						m_info.cylinders = totalheads / curheads; m_info.heads = curheads; m_info.sectors = cursectors;
						return true;
					}
			}
}

bool harddisk_image_device::read(u32 lba, void *buffer)
{
	if (device_t::s_log) fprintf(device_t::s_log, "HDRD t=%.6f lba=%08X\n", double(shim_sched().time().m_seconds), lba);
	if (auto it = m_overlay.find(lba); it != m_overlay.end()) { memcpy(buffer, it->second.data(), 512); return true; }
	if (!m_f || !eiii::shim::valid_sector(lba, m_length)) return false;
	if (!eiii::shim::seek(m_f, u64(lba) * 512)) return false;
	return fread(buffer, 1, 512, m_f) == 512;
}

bool harddisk_image_device::write(u32 lba, void const *buffer)
{
	if (!m_f || !eiii::shim::valid_sector(lba, m_length)) return false;
	auto &v = m_overlay[lba]; v.assign((u8 const *)buffer, (u8 const *)buffer + 512); ++m_writes;
	return true;
}

static void put_le(std::string &s, u64 v, int bytes) { for (int i = 0; i < bytes; ++i) s += char((v >> (8 * i)) & 0xff); }
static u64 get_le(std::string const &s, size_t at, int bytes) { u64 v = 0; for (int i = 0; i < bytes; ++i) v |= u64(u8(s[at + size_t(i)])) << (8 * i); return v; }

std::string harddisk_image_device::export_changes() const
{
	// Never launder an imported unverified legacy overlay into a strong HDC2
	// token. A user may export/rebase on a verified image before using HDC2.
	std::string b = m_legacy_overlay ? "EIIIHDC1" : "EIIIHDC2";
	put_le(b, m_length, 8);
	put_le(b, m_legacy_overlay ? m_fingerprint : m_fingerprint_full, 8);
	put_le(b, m_overlay.size(), 4);
	b.reserve(b.size() + m_overlay.size() * 516);
	for (auto const &[lba, data] : m_overlay) { put_le(b, lba, 4); b.append(reinterpret_cast<char const *>(data.data()), 512); }
	return b;
}

bool harddisk_image_device::import_changes(std::string const &b, std::string &error)
{
	const bool legacy = b.size() >= 28 && b.compare(0, 8, "EIIIHDC1") == 0;
	const bool strong = b.size() >= 28 && b.compare(0, 8, "EIIIHDC2") == 0;
	if (!legacy && !strong) { error = "not a supported EIII hard disk change set"; return false; }
	u64 const len = get_le(b, 8, 8), fp = get_le(b, 16, 8), n = get_le(b, 24, 4);
	if (len != m_length || fp != (legacy ? m_fingerprint : m_fingerprint_full)) {
		error = "the saved hard disk changes belong to a different disk image"; return false;
	}
	if (n > (b.size() - 28) / 516 || b.size() != 28 + u64(n) * 516) {
		error = "hard disk change set is truncated"; return false;
	}
	std::map<u32, std::vector<u8>> ov;
	for (u64 i = 0; i < n; ++i)
	{
		size_t const at = size_t(28 + i * 516);
		u32 const lba = u32(get_le(b, at, 4));
		if (!eiii::shim::valid_sector(lba, m_length)) { error = "hard disk change set addresses sectors past the image"; return false; }
		ov[lba].assign(b.begin() + long(at + 4), b.begin() + long(at + 516));
	}
	m_overlay = std::move(ov);
	m_legacy_overlay = legacy;
	if (legacy && !m_overlay.empty())
		error = "legacy EIIIHDC1 overlay verifies only the first 64 KiB; "
		        "the identity of the remaining source image is unverified";
	return true;
}

// ---- CD-ROM (read-only single-track MODE1 ISO) ----
cdrom_image_device::cdrom_image_device(machine_config const &mc, char const *tag, device_t *owner, u32 clock)
	: device_t(mc, cdda_device::s_type, tag, owner, clock) { }

bool cdrom_image_device::load(std::string const &path)
{
	unload();
	m_f = eiii::shim::fopen_utf8(path, "rb");
	if (!m_f) return false;
	u64 size = 0;
	if (!eiii::shim::file_size(m_f, size) ||
	    (size % 2048) != 0 || size / 2048 > UINT32_MAX) {
		unload(); return false;
	}
	// B02: bind a restore candidate to the entire ISO image rather than
	// merely its byte count, TOC or first 64 KiB. Non-audio loader thread
	// only; never hash/read a disk image inside the DAW process() callback.
	if (!eiii::shim::seek(m_f, 0)) { unload(); return false; }
	eiii::shim::image_fingerprint hash;
	u8 buffer[65536];
	for (;;) {
		const size_t n = fread(buffer, 1, sizeof buffer, m_f);
		if (n) hash.update(buffer, n);
		if (n != sizeof buffer) break;
	}
	if (ferror(m_f) || hash.seen != size ||
	    !eiii::shim::seek(m_f, 0)) {
		unload(); return false;
	}
	m_frames = u32(size / 2048);
	m_length = size;
	m_fingerprint = hash.full;
	++m_seq;
	return true;
}

void cdrom_image_device::unload()
{
	if (m_f) { fclose(m_f); m_f = nullptr; ++m_seq; }
	m_frames = 0;
	m_length = 0;
	m_fingerprint = 0;
}

bool cdrom_image_device::read_data(u32 lba, void *buffer, u32 datatype, bool)
{
	if (device_t::s_log) fprintf(device_t::s_log, "CDRD t=%.6f lba=%08X type=%u f=%d frames=%u\n", double(shim_sched().time().m_seconds), lba, datatype, m_f != nullptr, m_frames);
	if (!m_f || lba >= m_frames) return false;
	if (datatype != cdrom_file::CD_TRACK_MODE1 && datatype != cdrom_file::CD_TRACK_RAW_DONTCARE) return false;
	if (!eiii::shim::seek(m_f, u64(lba) * 2048)) return false;
	return fread(buffer, 1, 2048, m_f) == 2048;
}

device_type_impl const cdda_device::s_type{ "shim_device", "shim device" };
