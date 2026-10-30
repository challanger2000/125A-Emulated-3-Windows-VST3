#ifdef NDEBUG
#undef NDEBUG
#endif
#include "emu.h"
#include "../vst3-port/core_serial_section.h"
#include "../vst3-port/core_machine_context.h"
#include <cassert>
#include <limits>
int main(){
 using namespace eiii::vst3;
 CoreSerialSection::State state{},copy{};
 // An in-flight 8N1 byte with a half-bit receive clock and a parked TX clock.
 auto& a=state[0];a.df_start_bit_count=1;a.df_word_length=8;
 a.df_stop_bit_count=1;a.df_min_rx_stop_bit_count=1;
 a.rcv_flags=2;a.rcv_register_data=0xa500;a.rcv_bit_count_received=4;
 a.rcv_bit_count=9;a.rcv_rate=attotime::from_ticks(1,62500);
 a.tra_rate=attotime::never;a.rcv_line=1;a.tra_clock_state=1;
 std::vector<uint8_t> bytes,again;
 assert(CoreSerialSection::encode(state,bytes));
 assert(CoreSerialSection::decode(bytes.data(),bytes.size(),copy));
 assert(copy[0].rcv_rate==a.rcv_rate && copy[0].tra_rate==a.tra_rate);
 assert(copy[0].rcv_register_data==a.rcv_register_data && copy[0].rcv_bit_count_received==4);
 assert(CoreSerialSection::encode(copy,again)&&bytes==again);
 auto corrupt=bytes;corrupt.back()^=1;
 assert(!CoreSerialSection::decode(corrupt.data(),corrupt.size(),copy));
 // rx_w() intentionally decrements u8(0) to 255 for the external-clock
 // start-bit workaround; the next clock wraps it back to zero.
 state[1].start_bit_hack_for_external_clocks=true;
 state[1].rcv_flags=2;state[1].rcv_rate=attotime::never;
 state[1].rcv_bit_count_received=255;
 assert(CoreSerialSection::encode(state,again));
 assert(CoreSerialSection::decode(again.data(),again.size(),copy));
 assert(copy[1].rcv_bit_count_received==255);
 state[1].rcv_flags=0;assert(!CoreSerialSection::encode(state,again));
 state[1].rcv_bit_count_received=17;assert(!CoreSerialSection::encode(state,again));
 CoreMachineContext::State context,decoded;
 context.rom=0x123456789abcdef0ull;context.overlays[7]=~context.rom;
 context.pit1_last=false;context.sync_pattern={0xff80,0x55aa};context.rr7p={255,7};
 assert(CoreMachineContext::encode(context,bytes));
 assert(CoreMachineContext::decode(bytes.data(),bytes.size(),decoded));
 assert(decoded.rom==context.rom&&decoded.overlays==context.overlays);
 assert(!decoded.pit1_last&&decoded.sync_pattern==context.sync_pattern&&decoded.rr7p==context.rr7p);
 assert(!CoreMachineContext::decode(bytes.data(),bytes.size()-1,decoded));
 assert(decoded.rom==context.rom);
}
