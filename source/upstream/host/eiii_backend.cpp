#include "eiii_backend.h"
#include <cmath>
#include <utility>

namespace eiii {

software_backend::software_backend(uint16_t const *sound_ram, size_t words, double output_rate, unsigned oversample)
	: m_fc(sound_ram, words), m_sample_words(words), m_rate(output_rate), m_fs(output_rate * oversample), m_os(oversample)
{
	m_cem.prepare(m_fs);
}

bool software_backend::valid_recall_state(recall_state const &s) noexcept
{
	if ((s.model_v2 && (!s.decimator.valid_internal_state() || s.decimator.ratio()!=s.oversample)) ||
	    s.model_v2 != s.cem_v2.has_value() ||
	    !std::isfinite(s.rate) || s.rate<=0 ||
	    !std::isfinite(s.sample_rate) || s.sample_rate<=0 ||
	    !std::isfinite(s.next) || s.next<0 ||
	    !std::isfinite(s.accr_l) || !std::isfinite(s.accr_r) ||
	    s.oversample==0 || s.decimation_phase>=s.oversample ||
	    (s.output.size() & 1u) || s.output.size()/2>s.output_frames)
		return false;
	for(float x:s.output)
		if (!std::isfinite(x)) return false;
	return true;
}

bool software_backend::capture_recall_state(recall_state &out) const
{
	if (m_v2 != bool(m_cem2)) return false;
	try {
		recall_state s{};
		for(unsigned v=0;v<16;++v) {
			s.voices[v]=m_fc.state(v);
			for(unsigned b=0;b<5;++b)s.cv[v][b]=m_cv[v][b];
		}
		s.cem_v1=m_cem;
		if(m_v2) s.cem_v2.emplace(*m_cem2);
		s.decimator=m_dec;
		s.model_v2=m_v2;s.rate=m_rate;s.sample_rate=m_fs;
		s.oversample=m_os;s.sample_words=m_sample_words;
		s.now=m_now;s.output_frames=m_output_frames;s.next=m_next;s.accr_l=m_accl;s.accr_r=m_accr;
		s.decimation_phase=m_k;s.output=m_out;
		if (!valid_recall_state(s)) return false;
		out=std::move(s);
		return true;
	} catch (...) { return false; }
}

bool software_backend::restore_recall_state(recall_state const &state)
{
	if (!valid_recall_state(state) ||
	    state.rate!=m_rate || state.sample_rate!=m_fs ||
	    state.oversample!=m_os || state.model_v2!=m_v2 ||
	    state.sample_words!=m_sample_words ||
	    m_v2!=bool(m_cem2))
		return false;
	try {
		// Deep-copy all potentially allocating state BEFORE touching running
		// voices. Instance-local F-Chip sample RAM pointer remains intact.
		recall_state prepared=state;
		std::unique_ptr<cem3387_v2_bank> new_v2;
		if(prepared.cem_v2)
			new_v2=std::make_unique<cem3387_v2_bank>(
				std::move(*prepared.cem_v2));
		// Value move/standard allocator swaps below do not call hardware,
		// advance emulated time or consult other plugin instances.
		m_cem=std::move(prepared.cem_v1);
		m_cem2.swap(new_v2);
		m_dec=std::move(prepared.decimator);
		m_out.swap(prepared.output);
		for(unsigned v=0;v<16;++v) {
			m_fc.state(v)=prepared.voices[v];
			for(unsigned b=0;b<5;++b)m_cv[v][b]=prepared.cv[v][b];
		}
		m_now=prepared.now;m_output_frames=prepared.output_frames;m_next=prepared.next;
		m_accl=prepared.accr_l;m_accr=prepared.accr_r;
		m_k=prepared.decimation_phase;
		return true;
	} catch (...) { return false; }
}

void software_backend::advance_to(uint64_t t20)
{
	if (t20 > m_now) { m_fc.advance(t20 - m_now); m_now = t20; }
}

void software_backend::use_model_v2(cem_calibration const &cal, unsigned oversample, cem_v2_options const &o)
{
	m_v2 = true;
	m_os = oversample;
	m_fs = m_rate * oversample;
	m_cem2 = std::make_unique<cem3387_v2_bank>();
	m_cem2->prepare(cal, m_fs, o);
	m_dec.design(m_fs, oversample);
	// replay the CV state the firmware may already have written
	for (unsigned v = 0; v < 16; ++v)
		for (unsigned b = 0; b < 5; ++b) m_cem2->voice(v).set_word(b, m_cv[v][b]);
	m_cem2->reset();
}

void software_backend::render_until(uint64_t t20)
{
	double const step = 20e6 / m_fs;
	if (m_v2)
	{
		while (m_next < double(t20))
		{
			advance_to(uint64_t(m_next));
			int16_t pcm[16];
			for (unsigned v = 0; v < 16; ++v) pcm[v] = m_fc.pcm(v);
			double l, r, ol, orr;
			m_cem2->process(pcm, l, r);
			if (m_dec.push(l, r, ol, orr)) { m_out.push_back(float(ol / 10.0)); m_out.push_back(float(orr / 10.0)); ++m_output_frames; }
			m_next += step;
		}
		advance_to(t20);
		return;
	}
	while (m_next < double(t20))
	{
		advance_to(uint64_t(m_next));
		int16_t pcm[16];
		for (unsigned v = 0; v < 16; ++v) pcm[v] = m_fc.pcm(v);   // held lane value (zero-order hold)
		double l, r;
		m_cem.process(pcm, l, r);
		m_accl += l; m_accr += r;
		if (++m_k == m_os)
		{
			m_out.push_back(float(m_accl / m_os / 10.0));
			m_out.push_back(float(m_accr / m_os / 10.0));
			++m_output_frames;
			m_accl = m_accr = 0; m_k = 0;
		}
		m_next += step;
	}
	advance_to(t20);
}

void software_backend::rf_write(uint64_t t20, unsigned voice, unsigned slot, uint32_t value) { render_until(t20); m_fc.rf_write(voice, slot, value); }
uint32_t software_backend::rf_read(uint64_t t20, unsigned voice, unsigned slot) { render_until(t20); return m_fc.rf_read(voice, slot); }
void software_backend::ctrl_write(uint64_t t20, unsigned voice, uint8_t byte) { render_until(t20); m_fc.ctrl_write(voice, byte); }
void software_backend::cv_write(uint64_t t20, unsigned bank, unsigned channel, uint16_t word)
{
	render_until(t20);
	if (bank < 5 && channel < 16)
	{
		if (m_v2) m_cem2->cv_write(bank, channel, word); else m_cem.cv_write(bank, channel, word);
		m_cv[15 - channel][bank] = word;
	}
}

} // namespace eiii
