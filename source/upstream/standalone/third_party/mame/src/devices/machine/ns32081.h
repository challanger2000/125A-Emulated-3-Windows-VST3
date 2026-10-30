// license:BSD-3-Clause
// copyright-holders:Patrick Mackinlay

#ifndef MAME_MACHINE_NS32081_H
#define MAME_MACHINE_NS32081_H

#pragma once

#include "cpu/ns32000/common.h"

class ns32081_device_base
	: public device_t
	, public ns32000_fpu_interface
{
protected:
	ns32081_device_base(machine_config const &mconfig, device_type type, char const *tag, device_t *owner, u32 clock);

	// device_t implementation
	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override ATTR_COLD;

	// ns32000_slave_interface implementation
	virtual void state_add(device_state_interface &parent, int &index) override;

	// slave interface handlers
	template <typename T> T read();
	template <typename T> void write(T data);

	// execution helpers
	bool decode(u8 const idbyte, u16 const opword);
	void execute();
	u16 status(int *icount);
	void complete(s32 param);

	// register helpers
	virtual void reg_get(unsigned const op_size, u64 &op_value, unsigned const reg) const = 0;
	virtual void reg_set(unsigned const reg, unsigned const op_size, u64 const op_value) = 0;

protected:
	emu_timer *m_complete;

	u32 m_fsr; // floating-point status register

	// operating state
	u32 m_state;
	u8 m_idbyte;
	u16 m_opword;
	struct operand
	{
		u32 expected;
		u32 issued;
		u64 value;
	}
	m_op[3];
	u16 m_status;
	u32 m_tcy;
};

// B02: public POD-like snapshot of the NS32081's architectural and
// in-flight slave state. Timers/scheduler are separate and mandatory
// for a complete machine restore.
struct ns32081_recall_state {
	u32 fsr, state;
	u8 idbyte;
	u16 opword, status;
	u32 tcy;
	struct operand { u32 expected, issued; u64 value; } op[3];
	u32 f[8];
};

class ns32081_device
	: public ns32081_device_base
	, public ns32000_slow_slave_interface
{
public:
	ns32081_recall_state capture_recall_state() const {
		ns32081_recall_state s{};
		s.fsr=m_fsr; s.state=m_state; s.idbyte=m_idbyte;
		s.opword=m_opword; s.status=m_status; s.tcy=m_tcy;
		for(unsigned i=0;i<3;++i) {
			s.op[i]={m_op[i].expected,m_op[i].issued,m_op[i].value};
		}
		for(unsigned i=0;i<8;++i) s.f[i]=m_f[i];
		return s;
	}
	bool restore_recall_state(ns32081_recall_state const &s) {
		// Reject malformed operand cursors before altering any field.
		for (auto const &o:s.op)
			if (o.issued>o.expected || o.expected>8) return false;
		m_fsr=s.fsr; m_state=s.state; m_idbyte=s.idbyte;
		m_opword=s.opword; m_status=s.status; m_tcy=s.tcy;
		for(unsigned i=0;i<3;++i) {
			m_op[i].expected=s.op[i].expected;
			m_op[i].issued=s.op[i].issued;
			m_op[i].value=s.op[i].value;
		}
		for(unsigned i=0;i<8;++i) m_f[i]=s.f[i];
		return true;
	}
	ns32081_device(machine_config const &mconfig, char const *tag, device_t *owner, u32 clock);

	// ns32000_slave_interface implementation
	virtual void state_add(device_state_interface &parent, int &index) override;

	// ns32000_slow_slave_interface implementation
	virtual u16 slow_status(int *icount = nullptr) override;
	virtual u16 slow_read() override;
	virtual void slow_write(u16 data) override;

protected:
	// device_t implementation
	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override ATTR_COLD;

	// register helpers
	virtual void reg_get(unsigned const op_size, u64 &op_value, unsigned const reg) const override;
	virtual void reg_set(unsigned const reg, unsigned const op_size, u64 const op_value) override;

private:
	// registers
	u32 m_f[8];
};

class ns32381_device
	: public ns32081_device_base
	, public ns32000_slow_slave_interface
	, public ns32000_fast_slave_interface
{
public:
	ns32381_device(machine_config const &mconfig, char const *tag, device_t *owner, u32 clock);

	// ns32000_slave_interface implementation
	virtual void state_add(device_state_interface &parent, int &index) override;

	// ns32000_slow_slave_interface implementation
	virtual u16 slow_status(int *icount = nullptr) override;
	virtual u16 slow_read() override;
	virtual void slow_write(u16 data) override;

	// ns32000_fast_slave_interface implementation
	virtual u32 fast_status(int *icount = nullptr) override;
	virtual u32 fast_read() override;
	virtual void fast_write(u32 data) override;

protected:
	// device_t implementation
	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override ATTR_COLD;

	// register helpers
	virtual void reg_get(unsigned const op_size, u64 &op_value, unsigned const reg) const override;
	virtual void reg_set(unsigned const reg, unsigned const op_size, u64 const op_value) override;

private:
	// registers
	u64 m_l[8];
};

DECLARE_DEVICE_TYPE(NS32081, ns32081_device)
DECLARE_DEVICE_TYPE(NS32381, ns32381_device)

#endif // MAME_MACHINE_NS32081_H
