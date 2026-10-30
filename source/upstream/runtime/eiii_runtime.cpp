// eiii::runtime — see eiii_runtime.h.  Device construction, timer order and the slice loop are the ones the
// standalone has been proven with (instruction-identical to MAME); only process globals became instance state.
#include "eiii_runtime_impl.h"
#include "eiii_bank_import.h"
#include "../standalone/shim/large_file_io.h"
#include "../standalone/shim/utf8_file_paths.h"
#include "../standalone/shim/image_fingerprint.h"
#include <algorithm>
#include <filesystem>
#include <cstdlib>
#include <cstdio>

namespace eiii {

// boot ROM halves (2 x 16 KiB, 27128): MAME's names first, then the names common in dump collections; any letter case
static char const *const k_rom_lo[] = { "e3-lsboot_ip381a_emu_systems_4088.ic3", "EIII_IP381A_L.Bin", nullptr };
static char const *const k_rom_hi[] = { "e3-msboot_ip380a_emu_systems_4088.ic4", "EIII_IP380A_R.Bin", nullptr };

// the file in `dir` with one of `names` (case-insensitive), "" if none
static std::string find_rom(std::string const &dir, char const *const *names)
{
	std::error_code ec;
	const auto utf8dir=shim::path_from_utf8(dir.empty() ? "." : dir);
	if(utf8dir.empty())return {};
	std::filesystem::directory_iterator it(utf8dir, ec), end;
	if (ec) return {};
	std::string found;
	auto lower = [](std::string v) { for (char &c : v) c = char(tolower((unsigned char)c)); return v; };
	int best = 1 << 30;
	for (; it != end; it.increment(ec))
	{
		if (ec) break;
		std::string const filename = shim::utf8_from_path(it->path().filename());
		for (int i = 0; names[i]; ++i)
			if (lower(filename) == lower(names[i]) && i < best) { best = i; found = shim::utf8_from_path(it->path()); }
	}
	return found;
}

void runtime::impl::bind()
{
	g_shim_sched = &sched;
	device_memory_interface::s_host_space = &m;
	device_t::s_log = (cfg.log && getenv("EIII_DEVLOG")) ? cfg.log : nullptr;
}

bool runtime::impl::open(runtime_config const &c, std::string &err)
{
	cfg = c;
	bind();
	m.log = cfg.log;
	m.log_voice_starts = cfg.log_voice_starts;
	m.log_leds = cfg.log_leds;
	m.log_midi_chain = cfg.log_midi_chain;

	auto load = [&](char const *const *names, int lane) {
		if (cfg.rom_dir.empty()) { err = "no boot ROM folder set"; return false; }
		std::string const path = find_rom(cfg.rom_dir, names);
		if (path.empty()) { err = std::string("boot ROM ") + names[0] + " not found in " + cfg.rom_dir; return false; }
		FILE *f = shim::fopen_utf8(path, "rb");
		if (!f) { err = "cannot open boot ROM " + path; return false; }
		std::vector<u8> b(0x4000); size_t const n = fread(b.data(), 1, b.size(), f); fclose(f);
		for (size_t i = 0; i < n; ++i) m.rom[i] = lane ? u16((m.rom[i] & 0x00ff) | (b[i] << 8)) : u16((m.rom[i] & 0xff00) | b[i]);
		return true;
	};
	if (!load(k_rom_lo, 0) || !load(k_rom_hi, 1)) return false;

	cpu = std::make_unique<ns32016_device>(mcfg, "maincpu", nullptr, 10'000'000);
	fpu = std::make_unique<ns32081_device>(mcfg, "fpu", nullptr, 10'000'000);
	cpu->set_fpu(static_cast<ns32000_fpu_interface *>(fpu.get()));
	m.cpu = cpu.get(); m.fpu = fpu.get();
	sched.m_cpu = cpu.get();

	// SCSI exactly as emu3.cpp: NCR5380 = external device 0, harddisk on scsi:1, cdrom on scsi:3 (block size 512)
	scsi = std::make_unique<nscsi_bus_device>(mcfg, "scsi", nullptr);
	hd.reset(eiii_make_hd(mcfg, "harddisk", nullptr));
	hd_img = std::make_unique<harddisk_image_device>(mcfg, "image", hd.get());
	cd.reset(eiii_make_cd(mcfg, "cdrom", nullptr));
	cd_img = std::make_unique<cdrom_image_device>(mcfg, "image", cd.get());
	cd_cdda = std::make_unique<cdda_device>(mcfg, "cdda", cd.get());
	cd->set_block_size(512);
	hdc = std::make_unique<ncr5380_device>(mcfg, "ncr5380", nullptr);
	// SCC exactly as emu3.cpp: SCC85230 at 16 MHz / 4, int output inverted into SCCINT; channels are child devices
	scc = std::make_unique<scc85230_device>(mcfg, "scc", nullptr, 16'000'000 / 4);
	scc_cha = std::make_unique<z80scc_channel>(mcfg, "cha", scc.get(), 0);
	scc_chb = std::make_unique<z80scc_channel>(mcfg, "chb", scc.get(), 0);
	m.scc = scc.get();
	machine_t *const mp = &m;
	scc->out_int_callback().set([mp](int s) { mp->irq_w(machine_t::SCCINT, !s); });
	m.hdc = hdc.get();
	// Physical WD1772 D0, native flux/index/motor/side behavior. The OS
	// addresses the FDC directly, independently of the SCSI D1/D3 bus.
	fdd = std::make_unique<floppy_image_device>(mcfg);
	fdc = std::make_unique<wd1772_device>(mcfg,"wd1772",nullptr,8'000'000);
	m.fdd=fdd.get();m.fdc=fdc.get();
	fdc->set_disable_motor_control(true);
	fdc->intrq_wr_callback().set([mp](int state){mp->irq_w(machine_t::FDCINT,state);});
	if(!cfg.floppy_path.empty() && !fdd->load(cfg.floppy_path,err))return false;
	if (cfg.hd_path.empty() && cfg.floppy_path.empty()) { err = "no OS disk set (D0 floppy or D1 hard disk)"; return false; }
	if (!cfg.hd_path.empty() && !hd_img->load(cfg.hd_path)) { err = "cannot open hard disk image " + cfg.hd_path; return false; }
	if (!cfg.cd_path.empty() && !cd_img->load(cfg.cd_path)) { err = "cannot open CD-ROM image " + cfg.cd_path; return false; }
	scsi->set_external_device(0, static_cast<nscsi_device_interface *>(hdc.get()));
	scsi->set_external_device(1, static_cast<nscsi_device_interface *>(hd.get()));
	scsi->set_external_device(3, static_cast<nscsi_device_interface *>(cd.get()));
	// extra drives: same device classes as the unit's own HD / CD (the proven EIII SCSI timing)
	std::vector<device_t *> extra_order;
	for (unsigned id = 0; id < 8; ++id)
	{
		scsi_target const &t = cfg.scsi[id];
		if (!scsi_extra_id(id) || t.kind == scsi_kind::none) continue;
		extra_target &x = extra[id];
		std::string const n = std::to_string(id);
		if (t.kind == scsi_kind::hd)
		{
			x.hd.reset(eiii_make_hd(mcfg, ("harddisk" + n).c_str(), nullptr));
			x.hd_img = std::make_unique<harddisk_image_device>(mcfg, "image", x.hd.get());
			if (!x.hd_img->load(t.path)) { err = "cannot open hard disk image " + t.path + " (SCSI " + n + ")"; return false; }
			scsi->set_external_device(int(id), static_cast<nscsi_device_interface *>(x.hd.get()));
			extra_order.insert(extra_order.end(), { x.hd.get(), x.hd_img.get() });
		}
		else
		{
			x.cd.reset(eiii_make_cd(mcfg, ("cdrom" + n).c_str(), nullptr));
			x.cd_img = std::make_unique<cdrom_image_device>(mcfg, "image", x.cd.get());
			x.cdda = std::make_unique<cdda_device>(mcfg, "cdda", x.cd.get());
			x.cd->set_block_size(512);
			if (!t.path.empty() && !x.cd_img->load(t.path)) { err = "cannot open CD-ROM image " + t.path + " (SCSI " + n + ")"; return false; }
			scsi->set_external_device(int(id), static_cast<nscsi_device_interface *>(x.cd.get()));
			extra_order.insert(extra_order.end(), { x.cd.get(), x.cd_img.get(), x.cdda.get() });
		}
	}
	hdc->irq_handler().set([mp](int s) { mp->irq_w(machine_t::HDINT, s); });

	// PIT clocks as in the MAME driver: ch0 20 MHz/2, ch1 16 MHz/16, ch2 unclocked
	m.pit[0].clock_hz = 10e6; m.pit[1].clock_hz = 1e6; m.pit[2].clock_hz = 0;
	m.pit[0].out_cb = [mp](bool o) { mp->irq_w(machine_t::TGINT, o); };
	m.pit[1].out_cb = [mp](bool o) { if (mp->pit1_last && !o && !BIT(mp->misc, 6)) mp->irq_w(machine_t::STINT, 0); mp->pit1_last = o; };

	// output hardware -> software CEM backend
	backend = std::make_unique<software_backend>(m.snd.data(), m.snd.size(), cfg.sample_rate);
	backend->start_at(0);
	if (cfg.cem_model == "v2")
	{
		cem_calibration cal;
		if (!cfg.cem_calib.empty()) { std::string const e = cal.load(cfg.cem_calib); if (!e.empty()) { err = "cem calibration: " + e; return false; } }
		backend->use_model_v2(cal);
	}
	else if (cfg.cem_model != "v1") { err = "cem model must be v1 or v2"; return false; }
	m.out = backend.get();

	// timers in the order the proven standalone created them: MIDI RxD clock, then the scanner IRQ re-assert
	midi_timer = std::make_unique<emu_timer>([this](s32) { midi_tick(); });
	scan_irq_timer = std::make_unique<emu_timer>([mp](s32) { if (mp->scan_count) mp->irq_w(machine_t::SCNINT, 0); });
	m.scan_timer = scan_irq_timer.get();
	// hard disk changes kept from an earlier run / a project
	for (unsigned id = 1; id < 8; ++id)
	{
		harddisk_image_device *const h = hd_image(id);
		if (!h || cfg.hd_changes[id].empty()) continue;
		std::string e;
		if (!h->import_changes(cfg.hd_changes[id], e))
		{
			overlay_restore_failed=true;
			warning = "SCSI " + std::to_string(id) + ": " + e + " (not applied)";
			fprintf(stderr, "eiii: %s\n", warning.c_str());
		}
		else if (!e.empty()) {
			// HDC1 is deliberately not silently upgraded/certified as a full
			// image match. Preserve old songs and surface this legacy caveat.
			warning = "SCSI " + std::to_string(id) + ": " + e;
			fprintf(stderr, "eiii: %s\n", warning.c_str());
		}
	}
	order = { cpu.get(), fpu.get(), scsi.get(), hd.get(), hd_img.get(), cd.get(), cd_img.get(), cd_cdda.get() };
	order.insert(order.end(), extra_order.begin(), extra_order.end());
	order.insert(order.end(), { hdc.get(), scc.get(), scc_cha.get(), scc_chb.get(), fdd.get(), fdc.get() });
	return true;
}

void runtime::impl::power_on()
{
	bind();
	// MAME order: resolve objects, then start and reset in device order
	for (device_t *d : order) d->resolve();
	for (device_t *d : order) d->start();
	for (device_t *d : order) d->reset();
	for (device_t *d : order) d->reset_after_children();
	fdc->set_floppy(fdd.get());fdc->dden_w(0);
	m.irq_w(machine_t::SCNINT, 1);
	scc->rxa_w(1);
	std::sort(midi.ev.begin(), midi.ev.end(), [](auto const &x, auto const &y) { return x.first < y.first; });
	if (cfg.midi_clock || !midi.ev.empty()) midi_timer->adjust(attotime::from_hz(31250), 0, attotime::from_hz(31250));
	powered = true;
}

void runtime::impl::midi_tick()
{
	double const now_s = double(sched.time().m_seconds);
	while (midi.next < midi.ev.size() && midi.ev[midi.next].first <= now_s + 1e-9) { for (u8 b2 : midi.ev[midi.next].second) midi.q.push_back(b2); ++midi.next; }
	if (midi.bit < 0 && midi.q_head < midi.q.size())
	{
		midi.cur = midi.q[midi.q_head++];
		if (midi.q_head == midi.q.size()) { midi.q.clear(); midi.q_head = 0; }
		midi.bit = 0; scc->rxa_w(0); return;                    // start bit
	}
	if (midi.bit >= 0 && midi.bit < 8) { scc->rxa_w(BIT(midi.cur, midi.bit)); ++midi.bit; return; }
	if (midi.bit == 8) { scc->rxa_w(1); midi.bit = -1; return; }   // stop bit
}

runtime::runtime() : m(std::make_unique<impl>()) { }
runtime::~runtime()
{
	// devices and timers unregister from the instance's scheduler: bind it while they are destroyed
	shim_scheduler *const prev = g_shim_sched;
	auto *const previous_space = device_memory_interface::s_host_space;
	FILE *const previous_log = device_t::s_log;
	const bool owned_scheduler = prev == &m->sched;
	const bool owned_space = previous_space == &m->m;
	g_shim_sched = &m->sched;
	m.reset();
	// Never leave TLS referring to the destroyed machine. A different live
	// instance's binding must survive disposal of an uncommitted candidate.
	g_shim_sched = owned_scheduler ? shim_default_scheduler() : prev;
	device_memory_interface::s_host_space = owned_space ? nullptr : previous_space;
	device_t::s_log = owned_space ? nullptr : previous_log;
}

bool runtime::open(runtime_config const &cfg, std::string &error)
{
	if (!m->open(cfg, error)) return false;
	m->power_on();
	return true;
}
bool runtime::open_unpowered(runtime_config const &cfg, std::string &error) { return m->open(cfg, error); }
void runtime::power_on() { m->power_on(); }

bool runtime::check_floppy_image(std::string const& path,std::string &err) {
	// Read-only preflight, separate temporary scheduler and machine context.
	// The disk is never attached to a live WD1772 before validation.
	if(path.empty()){err="choose a D0 floppy image";return false;}
	machine_config mc;floppy_image_device disk(mc);
	return disk.load(path,err);
}
bool runtime::mount_floppy(std::string const& path,std::string &err) {
	m->bind();
	if(!m->fdd || !m->fdc){err="D0 controller unavailable";return false;}
	// Prepare path storage before mutating the machine; a successful insert
	// must also update the runtime configuration used by project restoration.
	std::string staged_path=path;
	// First validate/parse and commit atomically inside the drive adapter.
	// A rejected disk MUST NOT interrupt the current firmware transfer.
	if(!m->fdd->load(path,err))return false;
	// Physical media replacement invalidates any old in-flight sector stream.
	// WD1772 force-interrupt D0 cancels the previous command, not the CPU.
	m->fdc->cmd_w(0xd0);
	m->cfg.floppy_path.swap(staged_path);
	return true;
}
void runtime::eject_floppy() {
	m->bind();
	if(!m->fdd)return;
	if(m->fdc)m->fdc->cmd_w(0xd0);
	m->fdd->eject();
	m->cfg.floppy_path.clear();
}
void runtime::bind_thread() { m->bind(); }

void runtime::scanner_push(uint8_t byte) { m->m.scanner_push(byte); }
void runtime::panel_switch(uint8_t code, bool down) { m->m.scanner_push(down ? 0xc0 : 0xc3); m->m.scanner_push(code); }
void runtime::extra_switch(uint8_t mask, bool down)
{
#if defined(_WIN32)
    if (mask == 0x08 && down) m->m.enter_reads_while_down = 0;
#endif
    if (down) m->m.extra_switches |= mask;
    else m->m.extra_switches &= u8(~mask);
#if defined(_WIN32)
    if (mask == 0x08 && !down)
        if (const char *path = std::getenv("EIII_DIAG_FILE"))
            if (FILE *fp = std::fopen(path, "a")) {
                std::fprintf(fp, "ENTER POLL: firmware_reads_during_press=%llu\\n",
                    (unsigned long long)m->m.enter_reads_while_down);
                std::fclose(fp);
            }
#endif
}
void runtime::set_adc_input(unsigned input, uint16_t value) { if (input < 8) m->m.adc_pot[input] = u16(value & 0x7fff); }
uint16_t runtime::adc_input(unsigned input) const { return input < 8 ? m->m.adc_pot[input] : 0; }
void runtime::midi_in(uint8_t const *bytes, size_t n) { m->midi.q.insert(m->midi.q.end(), bytes, bytes + n); }
void runtime::schedule_midi(double t, std::vector<uint8_t> bytes) { m->midi.ev.emplace_back(t, std::move(bytes)); }

static bool file_size(std::string const &path, uint64_t &size, std::string &error, char const *what)
{
	FILE *f = shim::fopen_utf8(path, "rb");
	if (!f) { error = std::string("cannot open ") + what + " " + path; return false; }
	uint64_t n=0;
	const bool size_ok=shim::file_size(f,n);
	fclose(f);
	if (!size_ok) { error=std::string("cannot measure ")+what+": "+path; return false; }
	if (n <= 0) { error = std::string(what) + " is empty or unreadable: " + path; return false; }
	size = uint64_t(n);
	return true;
}
bool runtime::check_cd_image(std::string const &path, std::string &error)
{
	uint64_t n;
	if (!file_size(path, n, error, "CD-ROM image")) return false;
	if (n % 2048) { error = "not a MODE1/2048 ISO image (size is not a multiple of 2048): " + path; return false; }
	return true;
}
bool runtime::check_hd_image(std::string const &path, std::string &error)
{
	uint64_t n;
	if (!file_size(path, n, error, "hard disk image")) return false;
	if (n % 512) { error = "not a raw hard disk image (size is not a multiple of 512): " + path; return false; }
	return true;
}
bool runtime::check_rom_dir(std::string const &dir, std::string &error)
{
	for (char const *const *names : { k_rom_lo, k_rom_hi })
	{
		std::string const path = find_rom(dir, names);
		if (path.empty()) { error = std::string("boot ROM ") + names[0] + " (or " + names[1] + ") not found in " + dir; return false; }
		uint64_t n;
		if (!file_size(path, n, error, "boot ROM")) return false;
		if (n != 16384) { error = "boot ROM " + path + " is " + std::to_string(n) + " bytes, expected 16384"; return false; }
	}
	return true;
}

bool runtime::mount_cd(std::string const &path, std::string &error) { return mount_cd(3, path, error); }
bool runtime::mount_converted_bank_cd(std::string const &source,
                                     std::string const &cache_dir, std::string &error)
{
	// Reject an unopened machine before performing expensive conversions or
	// creating cache files; the later D3 mount requires a live CD device.
	if (!m || !m->cd_image(3)) {
		error = "no active EIII CD-ROM drive at SCSI 3";
		return false;
	}
	if (!bank_import::needs_conversion(source)) {
		error = "source has no EIIIX/ESI-32 banks requiring conversion";
		return false;
	}
	std::error_code ec;
	std::filesystem::create_directories(shim::path_from_utf8(cache_dir), ec);
	if (ec) { error = "cannot create conversion cache: " + ec.message(); return false; }
	auto const target = bank_import::cached_path(source, cache_dir);
	if (!bank_import::cache_fresh(source, target)) {
		std::string summary;
		if (!bank_import::convert(source, target, summary, error)) return false;
	}
	return mount_cd(3, target, error);
}
void runtime::eject_cd() { eject_cd(3); }
bool runtime::mount_cd(unsigned id, std::string const &path, std::string &error)
{
	cdrom_image_device *const img = m->cd_image(id);
	if (!img) { error = "no CD-ROM drive at SCSI " + std::to_string(id); return false; }
	if (!check_cd_image(path, error)) return false;
	if (!img->load(path)) { error = "cannot open CD-ROM image " + path; return false; }   // load() bumps the media sequence
	(id == 3 ? m->cfg.cd_path : m->cfg.scsi[id].path) = path;
	return true;
}
bool runtime::eject_cd(unsigned id)
{
	cdrom_image_device *const img = m->cd_image(id);
	if (!img) return false;
	img->unload();
	(id == 3 ? m->cfg.cd_path : m->cfg.scsi[id].path).clear();
	return true;
}
scsi_kind runtime::drive_kind(unsigned id) const
{
	if (id == 1) return scsi_kind::hd;
	if (id == 3) return scsi_kind::cd;
	if (id >= 8) return scsi_kind::none;
	return m->extra[id].hd ? scsi_kind::hd : m->extra[id].cd ? scsi_kind::cd : scsi_kind::none;
}

void runtime::set_adc_source(std::function<void(double, int16_t[2])> source)
{
	if (!source) { m->m.adc_source = nullptr; return; }
	m->m.adc_source = [src = std::move(source)](double t, s16 *lr) { int16_t v[2] = { 0, 0 }; src(t, v); lr[0] = v[0]; lr[1] = v[1]; };
}

std::string runtime::hd_changes(unsigned id) const { harddisk_image_device *h = m->hd_image(id); return h ? h->export_changes() : std::string(); }
size_t runtime::hd_changed_sectors(unsigned id) const { harddisk_image_device *h = m->hd_image(id); return h ? h->changed_sectors() : 0; }
uint64_t runtime::hd_writes(unsigned id) const { harddisk_image_device *h = m->hd_image(id); return h ? h->writes() : 0; }
std::string const &runtime::open_warning() const { return m->warning; }

bool runtime::write_image_with_changes(std::string const &image, std::string const &changes, std::string const &out_path, std::string &error)
{
	if (out_path == image) { error = "choose a new file: the original image is never modified"; return false; }
	FILE *in = shim::fopen_utf8(image, "rb");
	if (!in) { error = "cannot open " + image; return false; }
	// Exclusive creation prevents a race with another process creating the chosen file.
	// Never truncate an existing image, even if it appeared after the UI check.
	FILE *out = shim::fopen_utf8(out_path, "wbx");
	if (!out) { fclose(in); error = "cannot create " + out_path; return false; }
	std::vector<char> buf(1 << 20);
	size_t n; uint64_t total = 0; bool ok = true;
	eiii::shim::image_fingerprint hashes;
	while ((n = fread(buf.data(), 1, buf.size(), in)) > 0) {
		hashes.update(buf.data(), n);
		if (fwrite(buf.data(), 1, n, out) != n) { error = "disk image copy write error"; ok = false; break; }
		total += n;
	}
    if (ferror(in)) { error = "disk image read error"; ok = false; }
    if (fclose(in) != 0) { error = "disk image close error"; ok = false; }
	auto le = [&](size_t at, int bytes) { uint64_t v = 0; for (int i = 0; i < bytes; ++i) v |= uint64_t(uint8_t(changes[at + size_t(i)])) << (8 * i); return v; };
	if (ok && !changes.empty())
	{
		const bool legacy = changes.size() >= 28 && changes.compare(0, 8, "EIIIHDC1") == 0;
		const bool strong = changes.size() >= 28 && changes.compare(0, 8, "EIIIHDC2") == 0;
		if ((!legacy && !strong) || le(8, 8) != total) {
			error = "the changes do not belong to " + image; ok = false;
		}
		uint64_t const count = ok ? le(24, 4) : 0;
		if (ok && le(16, 8) != (legacy ? hashes.prefix : hashes.full)) {
			error = "source disk has changed since the overlay was created"; ok = false;
		}
		if (ok && (count > (changes.size() - 28) / 516 ||
		           changes.size() != 28 + count * 516)) {
			error = "hard disk change set is truncated"; ok = false;
		}
		for (uint64_t i = 0; ok && i < count; ++i)
		{
			size_t const at = size_t(28 + i * 516);
            const uint64_t lba = le(at, 4);
            if (total < 512 || lba >= total / 512 || total % 512 != 0) {
                error = "overlay sector outside source disk image";
                ok = false;
                break;
            }
			#if defined(_WIN32)
            if (_fseeki64(out, static_cast<__int64>(le(at, 4) * 512), SEEK_SET) != 0 || fwrite(changes.data() + at + 4, 1, 512, out) != 512) { error = "write error"; ok = false; }
#else
            if (fseeko(out, off_t(le(at, 4) * 512), SEEK_SET) != 0 || fwrite(changes.data() + at + 4, 1, 512, out) != 512) { error = "write error"; ok = false; }
#endif
        }
    }
    if (fclose(out) != 0) ok = false;
	if (!ok) { if (error.empty()) error = "write error on " + out_path; shim::remove_utf8(out_path); }
	return ok;
}

uint16_t runtime::debug_read16(uint32_t a) const
{
	machine_t const &x = m->m;
	a &= 0xfffffe;
	if (a < 0x8000) return x.rom[(a >> 1) & 0x3fff];
	if (a >= 0x8000 && a < 0x28000) return x.ram[(a - 0x8000) >> 1];
	if (a >= 0x400000 && a < 0xc00000) return x.snd[(a - 0x400000) >> 1];
	return 0;
}
uint32_t runtime::debug_read32(uint32_t a) const { return uint32_t(debug_read16(a)) | (uint32_t(debug_read16(a + 2)) << 16); }

uint64_t runtime::cycles() const { return m->cpu ? uint64_t(m->cpu->total_cycles()) : 0; }
double runtime::seconds() const { return double(cycles()) / cpu_hz; }
int runtime::stop_code() const { return m->stop; }
void runtime::request_stop(int code) { if (code > m->stop) m->stop = code; }

void runtime::run_until_cycle(uint64_t c_target)
{
	impl &s = *m;
	auto &cpu = *s.cpu;
	attotime const chunk_end = attotime(attotime::T(c_target) / attotime::T(CPU_HZ));
	while (u64(cpu.total_cycles()) < c_target && !s.stop)
	{
		// next event: earliest MAME-style timer, the PIT's next output change, or the caller's boundary
		attotime target = std::min(s.sched.next_expiry(), chunk_end);
		for (int i = 0; i < 3; ++i)
		{
			double const e = s.m.pit_next_event(i);
			if (e < 1e29) target = std::min(target, attotime(attotime::T(e) / attotime::T(CPU_HZ)));
		}
		attotime const now = cpu.local_time();
		attotime upto = target;
		if (target > now)
		{
			// Convert event deadlines to whole CPU cycles. Account for floating-point conversion
			// error near an exact cycle boundary (observed: 0.99999999 cycles on Windows).
			// Do not round arbitrary sub-cycle timer events up.
			long double const dc = (target - now).m_seconds * attotime::T(CPU_HZ);
			int const budget = int(std::min<long double>(std::floor(dc + 1e-6L), 1e9L));
			if (budget >= 1)
			{
				cpu.run(budget);
				// a slice aborted by a newly armed timer ends at the CPU's current time
				if (cpu.local_time() < target) upto = cpu.local_time();
			}
		}
		s.sched.fire_due(upto);
		for (int i = 0; i < 3; ++i) s.m.pit_update(i, double(cpu.total_cycles()));
		// CPU execution OR dispatch of later timer deadlines is progress.
		// Keep the existing 10000-step limit for a genuinely stationary loop.
        if (s.watchdog.observe(cpu.total_cycles(),s.sched.m_basetime.m_seconds)) {
#if defined(_WIN32)
            // Save the same hardware timing evidence as stderr in the user-local diagnostic log.
            // Only executed on an unrecoverable emulator watchdog event, never on the DAW audio thread.
            if (const char *path = std::getenv("EIII_DIAG_FILE"))
                if (FILE *diag = std::fopen(path, "a")) {
                    std::fprintf(diag, "WATCHDOG DETAIL: time=%.9Lf cycle=%llu timer=%.9Lf target=%.9Lf spins=%d\\n",
                        cpu.local_time().m_seconds, (unsigned long long)cpu.total_cycles(),
                        s.sched.next_expiry().m_seconds, target.m_seconds, int(s.watchdog.stalled_steps()));
                    for (int pit = 0; pit < 3; ++pit) {
                        auto const &c = s.m.pit[pit];
                        std::fprintf(diag, "PIT%d: mode=%u loaded=%d gate=%d out=%d count=%u reload=%u bcd=%d delay=%u rem=%llu last=%.3f next=%.3f\\n",
                            pit, c.mode, c.loaded, c.gate, c.out, c.count, c.reload, c.bcd,
                            c.load_delay, (unsigned long long)c.rem, c.last_t, s.m.pit_next_event(pit));
                    }
                    std::fclose(diag);
                }
#endif
			fprintf(stderr, "WATCHDOG: emulated time stuck at %.9f s (cycle %llu); next timer %.9f; target was %.9f\n", double(cpu.local_time().m_seconds),
				(unsigned long long)cpu.total_cycles(), double(s.sched.next_expiry().m_seconds), double(target.m_seconds));
			for (int i = 0; i < 3; ++i) { auto const &c = s.m.pit[i]; fprintf(stderr, "  PIT%d mode %u loaded %d gate %d out %d count %u reload %u bcd %d load_delay %u rem %llu last_t %.3f next %.3f\n",
				i, c.mode, c.loaded, c.gate, c.out, c.count, c.reload, c.bcd, c.load_delay, (unsigned long long)c.rem, c.last_t, s.m.pit_next_event(i)); }
			s.stop = 2; }
	}
}

uint64_t runtime::audio_frame_position() const {return m->backend->audio_frame_position();}

void runtime::render_audio() { m->backend->render_until(m->m.t20()); }
std::vector<float> &runtime::audio() { return m->backend->audio(); }

bool runtime::capture_device_sections(std::vector<uint8_t>& out) const
{
	// Only safe on the emulator owner thread at a stopped instruction boundary.
	// All device sections and media context are captured at this same boundary.
	if (!m || !m->powered || m->stop || m->overlay_restore_failed || m->sched.m_cpu_running) return false;
	try {
		auto candidate=std::make_unique<impl::core_recall_state>();
		if (!m->capture_core_recall(*candidate)) return false;
		std::vector<uint8_t> encoded;
		if (!impl::encode_core_device_sections(*candidate,encoded)) return false;
		out.swap(encoded);
		return true;
	} catch (...) { return false; }
}

bool runtime::restore_device_sections(uint8_t const* bytes, size_t size)
{
    if (!bytes || !size || !m || !m->powered || m->stop ||
        m->sched.m_cpu_running) return false;
    try {
        auto cfg=m->cfg;
        for(unsigned id=1;id<8;++id)
            if(auto* image=m->hd_image(id)) cfg.hd_changes[id]=image->export_changes();
        std::string error;
        return open_restored(cfg,bytes,size,error);
    } catch (...) { return false; }
}

bool runtime::open_restored(runtime_config const& cfg, uint8_t const* bytes,
                            size_t size, std::string& error)
{
    // All decoding, media I/O, allocation, device assignment and verification
    // occur in an independent runtime. Failure cannot alter the destination.
    // No CPU instruction executes between construction and commit.
    if (!bytes || !size || (m && m->sched.m_cpu_running)) return false;
    bool ok=false;
    try {
        // Reject malformed transport/topology before opening and hashing media.
        std::vector<vst3::CoreSectionContainer::Section> sections;
        std::array<bool,8> disks{},cds{};disks[1]=true;cds[3]=true;
        for(unsigned id=4;id<=7;++id) {
            disks[id]=cfg.scsi[id].kind==scsi_kind::hd;
            cds[id]=cfg.scsi[id].kind==scsi_kind::cd;
        }
        if(!vst3::CoreSectionContainer::decode(bytes,size,sections) ||
           !vst3::CoreSectionRoster::valid(sections,disks,cds,!cfg.floppy_path.empty())) {
            error="B02 invalid snapshot container or SCSI topology";
            return false;
        }
        for(auto const& section:sections) {
            std::vector<uint8_t> payload;
            if(!vst3::CoreStateEnvelope::decode(section.bytes.data(),section.bytes.size(),payload)) {
                error="B02 invalid device envelope";return false;
            }
        }
        runtime candidate;
        if(!candidate.open(cfg,error) || !candidate.media_restore_valid()) {
            if(error.empty())error=candidate.open_warning();
        } else {
        auto base=std::make_unique<impl::core_recall_state>();
        auto state=std::make_unique<impl::core_recall_state>();
        if(candidate.m->capture_core_recall(*base) &&
           impl::stage_core_device_sections(bytes,size,*base,*state) &&
           candidate.m->restore_core_recall(*state)) {
            // Preserve the external ADC callback without invoking it.
            if(m)candidate.m->m.adc_source=m->m.adc_source;
            m.swap(candidate.m);
            ok=true;
        } else error="B02 device state validation or verification failed";
        }
    } catch (...) {
        error="B02 candidate construction failed";
    }
    // Candidate destruction unbinds the thread-local shim. Always rebind the
    // committed/unchanged destination after it has been destroyed.
    bind_thread();
    return ok;
}

bool runtime::media_restore_valid() const {return m && !m->overlay_restore_failed;}

runtime_config runtime::project_configuration() const
{
    // Avoid copying the stale/large launch overlays only to replace them.
    auto const& source=m->cfg;
    runtime_config config;
    config.rom_dir=source.rom_dir;config.hd_path=source.hd_path;
    config.cd_path=source.cd_path;config.floppy_path=source.floppy_path;
    for(unsigned id=0;id<8;++id)config.scsi[id]=source.scsi[id];
    config.cem_model=source.cem_model;config.cem_calib=source.cem_calib;
    config.sample_rate=source.sample_rate;config.midi_clock=source.midi_clock;
    config.log=source.log;config.log_voice_starts=source.log_voice_starts;
    config.log_leds=source.log_leds;config.log_midi_chain=source.log_midi_chain;
    for(unsigned id=1;id<8;++id)
        if(auto* image=m->hd_image(id))config.hd_changes[id]=image->export_changes();
    return config;
}

bool runtime::copy_volatile_memory(std::vector<uint16_t>& ram_out,
                                    std::vector<uint16_t>& samples_out) const
{
	// Must run on the owning emulator thread; never on the audio thread.
	if (!m || !m->powered) return false;
	std::vector<uint16_t> ram(m->m.ram.begin(),m->m.ram.end());
	std::vector<uint16_t> samples(m->m.snd.begin(),m->m.snd.end());
	ram_out.swap(ram);
	samples_out.swap(samples);
	return true;
}

bool runtime::restore_volatile_memory(const std::vector<uint16_t>& ram,
                                      const std::vector<uint16_t>& samples)
{
	// This interface is intentionally incapable of mutating a running machine.
	// Call on the owner thread, strictly between open_unpowered() and power_on().
	if (!m || m->powered || m->stop ||
	    ram.size() != m->m.ram.size() ||
	    samples.size() != m->m.snd.size()) return false;
	// The sizes were checked before touching either region. Both copies are
	// non-allocating because destination vectors are already correctly sized.
	std::copy(ram.begin(),ram.end(),m->m.ram.begin());
	std::copy(samples.begin(),samples.end(),m->m.snd.begin());
	return true;
}

void runtime::snapshot(panel_snapshot &o) const
{
	machine_t const &x = m->m;
	memcpy(o.lcd.ddram, x.lcd_ddram, sizeof o.lcd.ddram);
	memcpy(o.lcd.cgram, x.lcd_cgram, sizeof o.lcd.cgram);
	o.lcd.ac = u8(x.lcd_ac);
	o.lcd.cgram_active = x.lcd_cgram_active;
	o.lcd.display_on = x.lcd_display_on; o.lcd.cursor_on = x.lcd_cursor_on; o.lcd.blink_on = x.lcd_blink_on;
	o.lcd.disp_shift = u8(x.lcd_disp_shift); o.lcd.num_lines = u8(x.lcd_num_line);
	o.led_latch = x.leds; o.misc_latch = x.misc;
	for (unsigned channel = 0; channel < 8; ++channel)
		o.adc_inputs[channel] = x.adc_pot[channel];
	o.adc_valid = true;
	o.emu_seconds = seconds();
	o.running = m->powered && !m->stop;
	++o.serial;
}

} // namespace eiii
