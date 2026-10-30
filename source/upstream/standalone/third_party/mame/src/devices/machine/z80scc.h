// license:BSD-3-Clause
// copyright-holders:Joakim Larsson Edstrom
/***************************************************************************

    Z80-SCC Serial Communications Controller emulation

****************************************************************************
               _____   _____                          _____   _____
        AD1  1|*    \_/     |40 AD0             D1  1|*    \_/     |40 D0
        AD3  2|             |39 AD2             D3  2|             |39 D2
        AD5  3|             |38 AD4             D5  3|             |38 D4
        AD7  4|             |37 AD6             D7  4|             |37 D6
       _INT  5|             |36 _DS           _INT  5|             |36 _RD
        IEO  6|             |35 _AS            IEO  6|             |35 _WR
        IEI  7|             |34 R/_W           IEI  7|             |34 B/_A
    _INTACK  8|             |33 _CS0       _INTACK  8|             |33 _CE
        VCC  9|             |32 CS1            VCC  9|             |32 C/_D
   _W//REQA 10|             |31 GND       _W//REQA 10|             |31 GND
     _SYNCA 11|   Z8030     |30 _W/_REQ     _SYNCA 11|   Z8530     |30 _W/_REQB
     _RTxCA 12|   Z80C30    |29 _SYNCB      _RTxCA 12|   Z85C30    |29 _SYNCB
       RxDA 13|   Z80230    |28 _RTxCB        RxDA 13|   Z85230    |28 _RTxCB
     _TRxCA 14|             |27 RxDB        _TRxCA 14|             |27 RxDB
       TxDA 15|             |26 _TRxCB        TxDA 15|             |26 _TRxCB
 _DTR//REQA 16|             |25 TxDB    _DTR//REQA 16|             |25 TxDB
      _RTSA 17|             |24 _DTR/_REQB   _RTSA 17|             |24 _DTR/_REQB
      _CTSA 18|             |23 _RTSB        _CTSA 18|             |23 _RTSB
      _DCDA 19|             |22 _CTSB        _DCDA 19|             |22 _CTSB
       PCLK 20|_____________|21 _DCDB         PCLK 20|_____________|21 _DCDB
                  ZBUS                                Universal Bus

***************************************************************************/

#ifndef MAME_MACHINE_Z80SCC_H
#define MAME_MACHINE_Z80SCC_H

#pragma once

#include "machine/z80daisy.h"
#include "diserial.h"

//**************************************************************************
//  TYPE DEFINITIONS
//**************************************************************************

// ======================> z80scc_channel

class z80scc_device;

//class z80scc_channel : public z80sio_channel
class z80scc_channel : public device_t,
		public device_serial_interface
{
	friend class z80scc_device;

public:
	z80scc_channel(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock = 0);

	// device_serial_interface implementation
	virtual void tra_callback() override;
	virtual void tra_complete() override;
	virtual void rcv_callback() override;
	virtual void rcv_complete() override;

	// read register handlers
	uint8_t do_sccreg_rr0();
	uint8_t do_sccreg_rr1();
	uint8_t do_sccreg_rr2();
	uint8_t do_sccreg_rr3();
	uint8_t do_sccreg_rr4();
	uint8_t do_sccreg_rr5();
	uint8_t do_sccreg_rr6();
	uint8_t do_sccreg_rr7();
	uint8_t do_sccreg_rr7p(){ return 0; } // Needs to be implemented for Synchronous mode
	// uint8_t do_sccreg_rr8(); Short cutted due to frequent use
	uint8_t do_sccreg_rr9();
	uint8_t do_sccreg_rr10();
	uint8_t do_sccreg_rr11();
	uint8_t do_sccreg_rr12();
	uint8_t do_sccreg_rr13();
	uint8_t do_sccreg_rr14();
	uint8_t do_sccreg_rr15();

	// write register handlers
	void do_sccreg_wr0(uint8_t data);
	void do_sccreg_wr0_resets(uint8_t data);
	void do_sccreg_wr1(uint8_t data);
	void do_sccreg_wr2(uint8_t data);
	void do_sccreg_wr3(uint8_t data);
	void do_sccreg_wr4(uint8_t data);
	void do_sccreg_wr5(uint8_t data);
	void do_sccreg_wr6(uint8_t data);
	void do_sccreg_wr7(uint8_t data);
	void do_sccreg_wr7p(uint8_t data){}; // Needs to be implemented for Synchronous mode
	void do_sccreg_wr8(uint8_t data);
	void do_sccreg_wr9(uint8_t data);
	void do_sccreg_wr10(uint8_t data);
	void do_sccreg_wr11(uint8_t data);
	void do_sccreg_wr12(uint8_t data);
	void do_sccreg_wr13(uint8_t data);
	void do_sccreg_wr14(uint8_t data);
	void do_sccreg_wr15(uint8_t data);

	uint8_t control_read();
	void control_write(uint8_t data);

	uint8_t data_read();
	void data_write(uint8_t data);

	void receive_data(uint8_t data);
	void m_tx_fifo_rp_step();
	void m_rx_fifo_rp_step();
	uint8_t m_rx_fifo_rp_data();

	void write_rx(int state);
	void cts_w(int state);
	void dcd_w(int state);
	void rxc_w(int state);
	void txc_w(int state);
	void sync_w(int state);

	// B02 diagnostic snapshot of SCC channel FIFO cursors.
	struct fifo_recall_cursors {
		int rx_read, rx_write, rx_capacity;
		int tx_read, tx_write, tx_capacity;
	};
	fifo_recall_cursors capture_fifo_cursors() const {
		return {m_rx_fifo_rp, m_rx_fifo_wp, m_rx_fifo_sz,
		        m_tx_fifo_rp, m_tx_fifo_wp, m_tx_fifo_sz};
	}
	static bool valid_fifo_cursors(fifo_recall_cursors const &s) {
		return s.rx_capacity >= 2 && s.rx_capacity <= 9 &&
		       s.tx_capacity >= 1 && s.tx_capacity <= 4 &&
		       s.rx_read >= 0 && s.rx_read < s.rx_capacity &&
		       s.rx_write >= 0 && s.rx_write < s.rx_capacity &&
		       s.tx_read >= 0 && s.tx_read < s.tx_capacity &&
		       s.tx_write >= 0 && s.tx_write < s.tx_capacity;
	}
	// B02 partial channel FIFO state; includes all slots and read/write cursors.
	struct fifo_recall_state {
		fifo_recall_cursors cursors;
		uint8_t rx_data[9], rx_errors[9], tx_data[4], tx_errors[4];
	};
	fifo_recall_state capture_fifo_recall() const {
		fifo_recall_state s{};
		s.cursors = capture_fifo_cursors();
		for (int i=0;i<9;++i) {
			s.rx_data[i]=m_rx_data_fifo[i];
			s.rx_errors[i]=m_rx_error_fifo[i];
		}
		for (int i=0;i<4;++i) {
			s.tx_data[i]=m_tx_data_fifo[i];
			s.tx_errors[i]=m_tx_error_fifo[i];
		}
		return s;
	}
	bool restore_fifo_recall(fifo_recall_state const &s) {
		if (!valid_fifo_cursors(s.cursors) ||
		    s.cursors.rx_capacity != m_rx_fifo_sz ||
		    s.cursors.tx_capacity != m_tx_fifo_sz) return false;
		for (int i=0;i<9;++i) {
			m_rx_data_fifo[i]=s.rx_data[i];
			m_rx_error_fifo[i]=s.rx_errors[i];
		}
		for (int i=0;i<4;++i) {
			m_tx_data_fifo[i]=s.tx_data[i];
			m_tx_error_fifo[i]=s.tx_errors[i];
		}
		m_rx_fifo_rp=s.cursors.rx_read;
		m_rx_fifo_wp=s.cursors.rx_write;
		m_tx_fifo_rp=s.cursors.tx_read;
		m_tx_fifo_wp=s.cursors.tx_write;
		return true;
	}
	// B02 partial SCC register-bank recall. Register 9 is owned by the
	// parent controller; a channel has no WR9 storage.
	struct register_recall_state {
		uint8_t rr[16], wr[16];
		uint8_t wr7p;
	};
	register_recall_state capture_register_recall() const {
		register_recall_state s{};
		const uint8_t rr[]={m_rr0,m_rr1,m_rr2,m_rr3,m_rr4,m_rr5,m_rr6,m_rr7,m_rr8,m_rr9,m_rr10,m_rr11,m_rr12,m_rr13,m_rr14,m_rr15};
		const uint8_t wr[]={m_wr0,m_wr1,m_wr2,m_wr3,m_wr4,m_wr5,m_wr6,m_wr7,m_wr8,0,m_wr10,m_wr11,m_wr12,m_wr13,m_wr14,m_wr15};
		for(int i=0;i<16;++i){s.rr[i]=rr[i];s.wr[i]=wr[i];}
		s.wr7p=m_wr7p;
		return s;
	}
	void restore_register_recall(register_recall_state const &s) {
		uint8_t *rr[]={&m_rr0,&m_rr1,&m_rr2,&m_rr3,&m_rr4,&m_rr5,&m_rr6,&m_rr7,&m_rr8,&m_rr9,&m_rr10,&m_rr11,&m_rr12,&m_rr13,&m_rr14,&m_rr15};
		uint8_t *wr[]={&m_wr0,&m_wr1,&m_wr2,&m_wr3,&m_wr4,&m_wr5,&m_wr6,&m_wr7,&m_wr8,nullptr,&m_wr10,&m_wr11,&m_wr12,&m_wr13,&m_wr14,&m_wr15};
		for(int i=0;i<16;++i){*rr[i]=s.rr[i];if(wr[i])*wr[i]=s.wr[i];}
		m_wr7p=s.wr7p;
	}
	// B02 channel receive/transmit timing and interrupt latch state.
	// Timer deadlines are restored separately by shim_scheduler.
	struct channel_line_recall_state {
		uint16_t brg_counter;
		unsigned brg_rate, delayed_tx_brg_change;
		uint8_t rx_error, tx_error, extint_latch, extint_states;
		int rx_clock, rx_first, rx_break, rxd, ri;
		int tx_clock, tx_int_disarm, dtr, rts, rcv_mode, ph, rxc, txc;
	};
	channel_line_recall_state capture_channel_line_recall() const {
		return {m_brg_counter,m_brg_rate,m_delayed_tx_brg_change,
		        m_rx_error,m_tx_error,m_extint_latch,m_extint_states,
		        m_rx_clock,m_rx_first,m_rx_break,m_rxd,m_ri,
		        m_tx_clock,m_tx_int_disarm,m_dtr,m_rts,m_rcv_mode,
		        m_ph,m_rxc,m_txc};
	}
	void restore_channel_line_recall(channel_line_recall_state const &s) {
		m_brg_counter=s.brg_counter; m_brg_rate=s.brg_rate;
		m_delayed_tx_brg_change=s.delayed_tx_brg_change;
		m_rx_error=s.rx_error; m_tx_error=s.tx_error;
		m_extint_latch=s.extint_latch; m_extint_states=s.extint_states;
		m_rx_clock=s.rx_clock; m_rx_first=s.rx_first; m_rx_break=s.rx_break;
		m_rxd=s.rxd; m_ri=s.ri; m_tx_clock=s.tx_clock;
		m_tx_int_disarm=s.tx_int_disarm; m_dtr=s.dtr; m_rts=s.rts;
		m_rcv_mode=s.rcv_mode; m_ph=s.ph; m_rxc=s.rxc; m_txc=s.txc;
	}
	uint16_t recall_sync_pattern() const {return m_sync_pattern;}
	void restore_sync_pattern(uint16_t value) {m_sync_pattern=value;}
	TIMER_CALLBACK_MEMBER(brg_tick);

	int m_rxc = 0;
	int m_txc = 0;

	// Register state
	// read registers     enum
	uint8_t m_rr0; // REG_RR0_STATUS
	uint8_t m_rr1; // REG_RR1_SPEC_RCV_COND
	uint8_t m_rr2; // REG_RR2_INTERRUPT_VECT
	uint8_t m_rr3; // REG_RR3_INTERUPPT_PEND
	uint8_t m_rr4; // REG_RR4_WR4_OR_RR0
	uint8_t m_rr5; // REG_RR5_WR5_OR_RR0
	uint8_t m_rr6; // REG_RR6_LSB_OR_RR2
	uint8_t m_rr7; // REG_RR7_MSB_OR_RR3
	uint8_t m_rr7p;
	uint8_t m_rr8; // REG_RR8_RECEIVE_DATA
	uint8_t m_rr9; //  REG_RR9_WR3_OR_RR13
	uint8_t m_rr10; // REG_RR10_MISC_STATUS
	uint8_t m_rr11; // REG_RR11_WR10_OR_RR15
	uint8_t m_rr12; // REG_RR12_LO_TIME_CONST
	uint8_t m_rr13; // REG_RR13_HI_TIME_CONST
	uint8_t m_rr14; // REG_RR14_WR7_OR_R10
	uint8_t m_rr15; // REG_RR15_WR15_EXT_STAT

	// write registers    enum
	uint8_t m_wr0; // REG_WR0_COMMAND_REGPT
	uint8_t m_wr1; // REG_WR1_INT_DMA_ENABLE
	uint8_t m_wr2; // REG_WR2_INT_VECTOR
	uint8_t m_wr3; // REG_WR3_RX_CONTROL
	uint8_t m_wr4; // REG_WR4_RX_TX_MODES
	uint8_t m_wr5; // REG_WR5_TX_CONTROL
	uint8_t m_wr6; // REG_WR6_SYNC_OR_SDLC_A
	uint8_t m_wr7; // REG_WR7_SYNC_OR_SDLC_F
	uint8_t m_wr7p; //
	uint8_t m_wr8;  // REG_WR8_TRANSMIT_DATA
	//  uint8_t m_wr9;  // REG_WR9_MASTER_INT_CTRL
	uint8_t m_wr10; // REG_WR10_MSC_RX_TX_CTRL
	uint8_t m_wr11; // REG_WR11_CLOCK_MODES
	uint8_t m_wr12; // REG_WR12_LO_BAUD_GEN
	uint8_t m_wr13; // REG_WR13_HI_BAUD_GEN
	uint8_t m_wr14; // REG_WR14_MISC_CTRL
	uint8_t m_wr15; // REG_WR15_EXT_ST_INT_CTRL

protected:
	enum
	{
		RCV_IDLE                = 0,
		RCV_SEEKING             = 1,
		RCV_SAMPLING            = 2
	};

	enum
	{
		INT_TRANSMIT            = 0,
		INT_EXTERNAL            = 1,
		INT_RECEIVE             = 2,
		INT_SPECIAL             = 3,
	};

	enum
	{
		INT_TRANSMIT_PRIO       = 1,
		INT_EXTERNAL_PRIO       = 0,
		INT_RECEIVE_PRIO        = 2,
		INT_SPECIAL_PRIO        = 0,
	};

	// Read registers
	enum
	{
		REG_RR0_STATUS          = 0,
		REG_RR1_SPEC_RCV_COND   = 1,
		REG_RR2_INTERRUPT_VECT  = 2,
		REG_RR3_INTERUPPT_PEND  = 3,
		REG_RR4_WR4_OR_RR0      = 4,
		REG_RR5_WR5_OR_RR0      = 5,
		REG_RR6_LSB_OR_RR2      = 6,
		REG_RR7_MSB_OR_RR3      = 7,
		REG_RR8_RECEIVE_DATA    = 8,
		REG_RR9_WR3_OR_RR13     = 9,
		REG_RR10_MISC_STATUS    = 10,
		REG_RR11_WR10_OR_RR15   = 11,
		REG_RR12_LO_TIME_CONST  = 12,
		REG_RR13_HI_TIME_CONST  = 13,
		REG_RR14_WR7_OR_R10     = 14,
		REG_RR15_WR15_EXT_STAT  = 15
	};

	// Write registers
	enum
	{
		REG_WR0_COMMAND_REGPT   = 0,
		REG_WR1_INT_DMA_ENABLE  = 1,
		REG_WR2_INT_VECTOR      = 2,
		REG_WR3_RX_CONTROL      = 3,
		REG_WR4_RX_TX_MODES     = 4,
		REG_WR5_TX_CONTROL      = 5,
		REG_WR6_SYNC_OR_SDLC_A  = 6,
		REG_WR7_SYNC_OR_SDLC_F  = 7,
		REG_WR8_TRANSMIT_DATA   = 8,
		REG_WR9_MASTER_INT_CTRL = 9,
		REG_WR10_MSC_RX_TX_CTRL = 10,
		REG_WR11_CLOCK_MODES    = 11,
		REG_WR12_LO_BAUD_GEN    = 12,
		REG_WR13_HI_BAUD_GEN    = 13,
		REG_WR14_MISC_CTRL      = 14,
		REG_WR15_EXT_ST_INT_CTRL= 15
	};

	// device-level overrides
	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override ATTR_COLD;

	unsigned int get_brg_rate();
	unsigned int get_rtxc_rate();
	void update_baudtimer();

	void scc_register_write(uint8_t reg, uint8_t data);
	uint8_t scc_register_read(uint8_t reg);
	void update_serial();
	void set_dtr(int state);
	void set_rts(int state);

	int get_clock_mode();
	void update_rts();
	stop_bits_t get_stop_bits();
	int get_rx_word_length();
	int get_tx_word_length();
	void safe_transmit_register_reset();
	void check_dma_request();
	void check_receive_interrupt();

	emu_timer *m_baudtimer;
	uint16_t m_brg_counter;
	unsigned int m_brg_rate;
	unsigned int m_delayed_tx_brg_change;

	// receiver state
	uint8_t m_rx_data_fifo[8 + 1];    // receive data FIFO (ring buffer, one slot is always left unused)
	uint8_t m_rx_error_fifo[8 + 1];   // receive error FIFO
	uint8_t m_rx_error;       // current receive error
	//int m_rx_fifo         // receive FIFO pointer
	int m_rx_fifo_rp;       // receive FIFO read pointer
	int m_rx_fifo_wp;       // receive FIFO write pointer
	int m_rx_fifo_sz;       // receive FIFO ring size (FIFO depth + 1)

	int m_rx_clock;         // receive clock pulse count
	int m_rx_first;         // first character received
	int m_rx_break;         // receive break condition

	uint8_t m_extint_latch;    // external/status Int latch enable
	uint8_t m_extint_states;   // external/status Int latches state

	int m_rxd;
	int m_ri = 0;       // ring indicator latch

	// transmitter state
	uint8_t m_tx_data_fifo[4];  // data FIFO
	uint8_t m_tx_error_fifo[4]; // error FIFO
	int m_tx_fifo_rp;           // FIFO read pointer
	int m_tx_fifo_wp;           // FIFO write pointer
	int m_tx_fifo_sz;           // FIFO size
	uint8_t m_tx_error = 0;         // current error
	int m_tx_clock;             // transmit clock pulse count
	int m_tx_int_disarm;        // temp Tx int disarm until next byte written


	int m_dtr;      // data terminal ready
	int m_rts;      // request to send

	// synchronous state
	uint16_t m_sync_pattern;      // sync character

	int m_rcv_mode = 0;
	int m_index;
	z80scc_device *m_uart;

	// SCC specifics
	int m_ph = 0;       // Point high command to access regs 08-0f
	uint8_t m_zc;

private:
	// helpers
	void out_txd_cb(int state);
	void out_rts_cb(int state);
	void out_dtr_cb(int state);

};


// ======================> z80scc_device

class z80scc_device : public device_t, public device_z80daisy_interface
{
	friend class z80scc_channel;

public:
	auto out_txda_callback() { return m_out_txd_cb[0].bind(); }
	auto out_dtra_callback() { return m_out_dtr_cb[0].bind(); }
	auto out_rtsa_callback() { return m_out_rts_cb[0].bind(); }
	auto out_wreqa_callback() { return m_out_wreq_cb[0].bind(); }
	auto out_synca_callback() { return m_out_sync_cb[0].bind(); }
	auto out_rxdrqa_callback() { return m_out_rxdrq_cb[0].bind(); }
	auto out_txdrqa_callback() { return m_out_txdrq_cb[0].bind(); }
	auto out_txdb_callback() { return m_out_txd_cb[1].bind(); }
	auto out_dtrb_callback() { return m_out_dtr_cb[1].bind(); }
	auto out_rtsb_callback() { return m_out_rts_cb[1].bind(); }
	auto out_wreqb_callback() { return m_out_wreq_cb[1].bind(); }
	auto out_syncb_callback() { return m_out_sync_cb[1].bind(); }
	auto out_rxdrqb_callback() { return m_out_rxdrq_cb[1].bind(); }
	auto out_txdrqb_callback() { return m_out_txdrq_cb[1].bind(); }
	auto out_int_callback() { return m_out_int_cb.bind(); }

	void set_cputag(const char *tag) { m_cputag = tag; }

	void configure_channels(int rxa, int txa, int rxb, int txb)
	{
		m_rxca = rxa;
		m_txca = txa;
		m_rxcb = rxb;
		m_txcb = txb;
	}

	uint8_t dc_ab_r(offs_t offset);
	void dc_ab_w(offs_t offset, uint8_t data);
	uint8_t ab_dc_r(offs_t offset);
	void ab_dc_w(offs_t offset, uint8_t data);

	/* Definitions moved to z80scc.c for enhancements */
	uint8_t da_r(offs_t offset);
	void da_w(offs_t offset, uint8_t data);
	uint8_t db_r(offs_t offset);
	void db_w(offs_t offset, uint8_t data);

	uint8_t ca_r(offs_t offset)            { return m_chanA->control_read(); }
	void ca_w(offs_t offset, uint8_t data) { m_chanA->control_write(data); }
	uint8_t cb_r(offs_t offset)            { return m_chanB->control_read(); }
	void cb_w(offs_t offset, uint8_t data) { m_chanB->control_write(data); }

	uint8_t zbus_r(offs_t offset);
	void zbus_w(offs_t offset, uint8_t data);

	// B02: SCC controller interrupt state, excluding channel FIFOs and
	// serial shift registers. Invoke on owner thread with CPU stopped.
	struct controller_recall_state {
		uint8_t wr9, wr0_ptrbits;
		int out_int_state, int_state[6], int_source[6];
	};
	controller_recall_state capture_controller_recall() const {
		controller_recall_state s{};
		s.wr9 = m_wr9; s.wr0_ptrbits = m_wr0_ptrbits;
		s.out_int_state = m_out_int_state;
		for (int i=0;i<6;++i) {
			s.int_state[i] = m_int_state[i];
			s.int_source[i] = m_int_source[i];
		}
		return s;
	}
	static bool valid_controller_recall(controller_recall_state const &s) {
		if (s.wr0_ptrbits > 0x0f ||
		    (s.out_int_state != 0 && s.out_int_state != 1)) return false;
		for (int i=0;i<6;++i)
			if ((s.int_state[i] & ~3) != 0 ||
			    s.int_source[i] < 0 || s.int_source[i] > 3) return false;
		return true;
	}
	bool restore_controller_recall(controller_recall_state const &s) {
		if (!valid_controller_recall(s)) return false;
		m_wr9=s.wr9; m_wr0_ptrbits=s.wr0_ptrbits;
		m_out_int_state=s.out_int_state;
		for (int i=0;i<6;++i) {
			m_int_state[i]=s.int_state[i];
			m_int_source[i]=s.int_source[i];
		}
		// No callback: motherboard IRQ and CPU input lines must be
		// restored together in the later complete machine transaction.
		return true;
	}
	// interrupt acknowledge
	int m1_r();

	// Single registers instances accessed from both channels
	uint8_t m_wr9;  // REG_WR9_MASTER_INT_CTRL

	void rxa_w(int state) { m_chanA->write_rx(state); }
	void rxb_w(int state) { m_chanB->write_rx(state); }
	void ctsa_w(int state) { m_chanA->cts_w(state); }
	void ctsb_w(int state) { m_chanB->cts_w(state); }
	void dcda_w(int state) { m_chanA->dcd_w(state); }
	void dcdb_w(int state) { m_chanB->dcd_w(state); }
	void rxca_w(int state) { m_chanA->rxc_w(state); }
	void rxcb_w(int state) { m_chanB->rxc_w(state); }
	void txca_w(int state) { m_chanA->txc_w(state); }
	void txcb_w(int state) { m_chanB->txc_w(state); }
	void rxtxcb_w(int state) { m_chanB->rxc_w(state); m_chanB->txc_w(state); }
	void synca_w(int state) { m_chanA->sync_w(state); }
	void syncb_w(int state) { m_chanB->sync_w(state); }
	int update_extint(int i);
	int get_extint_priority(int type);

protected:
	z80scc_device(const machine_config &mconfig, device_type type, const char *tag, device_t *owner, uint32_t clock, uint32_t variant);

	// device_t implementation
	virtual void device_start() override ATTR_COLD;
	virtual void device_reset_after_children() override;
	virtual void device_add_mconfig(machine_config &config) override ATTR_COLD;

	// device_z80daisy_interface implementation
	virtual int z80daisy_irq_state() override;
	virtual int z80daisy_irq_ack() override;
	virtual void z80daisy_irq_reti() override;

	// internal interrupt management
	void check_interrupts();
	void reset_interrupts(int index);
	uint8_t modify_vector(uint8_t vect, int i, uint8_t src);
	void trigger_interrupt(int index, int state);


	int get_channel_index(z80scc_channel *ch) { return (ch == m_chanA) ? 0 : 1; }

	// Variants in the SCC family
	enum
	{
		TYPE_SCC8030  = 0x002,
		TYPE_SCC80C30 = 0x004,
		TYPE_SCC80230 = 0x008,
		TYPE_SCC8530  = 0x010,
		TYPE_SCC85C30 = 0x020,
		TYPE_SCC85230 = 0x040,
		TYPE_SCC85233 = 0x080,
		TYPE_SCC8523L = 0x100,

		SET_NMOS   = TYPE_SCC8030  | TYPE_SCC8530,
		SET_CMOS   = TYPE_SCC80C30 | TYPE_SCC85C30,
		SET_ESCC   = TYPE_SCC80230 | TYPE_SCC85230 | TYPE_SCC8523L,
		SET_EMSCC  = TYPE_SCC85233,
		SET_Z80X30 = TYPE_SCC8030  | TYPE_SCC80C30 | TYPE_SCC80230,
		SET_Z85X3X = TYPE_SCC8530  | TYPE_SCC85C30 | TYPE_SCC85230 | TYPE_SCC8523L | TYPE_SCC85233
	};

	enum
	{
		CHANNEL_A = 0,
		CHANNEL_B
	};

	required_device<z80scc_channel> m_chanA;
	required_device<z80scc_channel> m_chanB;

	// internal state
	int m_rxca;
	int m_txca;
	int m_rxcb;
	int m_txcb;

	// internal state
	devcb_write_line::array<2> m_out_txd_cb;
	devcb_write_line::array<2> m_out_dtr_cb;
	devcb_write_line::array<2> m_out_rts_cb;
	devcb_write_line::array<2> m_out_wreq_cb;
	devcb_write_line::array<2> m_out_sync_cb;
	devcb_write_line::array<2> m_out_rxdrq_cb;
	devcb_write_line::array<2> m_out_txdrq_cb;

	devcb_write_line    m_out_int_cb;

	int m_out_int_state;
	int m_int_state[6]; // interrupt state
	int m_int_source[6]{}; // interrupt source

	int const m_variant;
	uint8_t m_wr0_ptrbits;
	const char *m_cputag;
};

class scc8030_device : public z80scc_device
{
public:
	scc8030_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock = 0);
};

class scc80c30_device : public z80scc_device
{
public:
	scc80c30_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock = 0);
};

class scc80230_device : public z80scc_device
{
public:
	scc80230_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock = 0);
};

class scc8530_device : public z80scc_device
{
public:
	scc8530_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock = 0);
};

class scc85c30_device : public z80scc_device
{
public:
	scc85c30_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock = 0);
};

class scc85230_device : public z80scc_device
{
public:
	scc85230_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock = 0);
};

class scc85233_device : public z80scc_device
{
public:
	scc85233_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock = 0);
};

class scc8523l_device : public z80scc_device
{
public:
	scc8523l_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock = 0);
};

// device type definition
DECLARE_DEVICE_TYPE(Z80SCC_CHANNEL, z80scc_channel)
DECLARE_DEVICE_TYPE(SCC8030,        scc8030_device)
DECLARE_DEVICE_TYPE(SCC80C30,       scc80c30_device)
DECLARE_DEVICE_TYPE(SCC80230,       scc80230_device)
DECLARE_DEVICE_TYPE(SCC8530,        scc8530_device)
DECLARE_DEVICE_TYPE(SCC85C30,       scc85c30_device)
DECLARE_DEVICE_TYPE(SCC85230,       scc85230_device)
DECLARE_DEVICE_TYPE(SCC85233,       scc85233_device)
DECLARE_DEVICE_TYPE(SCC8523L,       scc8523l_device)

#endif // MAME_MACHINE_Z80SCC_H
