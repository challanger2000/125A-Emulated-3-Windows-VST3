// Complete SCC asynchronous shift-register transport, including an in-flight byte.
#pragma once
#include "core_time.h"
#include <array>
#include "diserial.h"
namespace eiii::vst3 {
struct CoreSerialSection {
 using State=std::array<device_serial_interface::serial_recall_state,2>;
 static constexpr uint32_t tag=0x31524553u;
 static bool encode(State const& s,std::vector<uint8_t>& out){
  std::vector<uint8_t> b;CoreStateEnvelope::put32(b,tag);
  for(auto const& v:s){
   if(!device_serial_interface::valid_serial_recall(v))return false;
   CoreStateEnvelope::put32(b,uint32_t(v.start_bit_hack_for_external_clocks));
   CoreStateEnvelope::put32(b,uint32_t(v.df_start_bit_count));
   CoreStateEnvelope::put32(b,uint32_t(v.df_word_length));
   CoreStateEnvelope::put32(b,uint32_t(v.df_parity));
   CoreStateEnvelope::put32(b,uint32_t(v.df_stop_bit_count));
   CoreStateEnvelope::put32(b,uint32_t(v.df_min_rx_stop_bit_count));
   CoreStateEnvelope::put32(b,uint32_t(v.rcv_register_data));
   CoreStateEnvelope::put32(b,uint32_t(v.rcv_flags));
   CoreStateEnvelope::put32(b,uint32_t(v.rcv_bit_count_received));
   CoreStateEnvelope::put32(b,uint32_t(v.rcv_bit_count));
   CoreStateEnvelope::put32(b,uint32_t(v.rcv_byte_received));
   CoreStateEnvelope::put32(b,uint32_t(v.rcv_framing_error));
   CoreStateEnvelope::put32(b,uint32_t(v.rcv_parity_error));
   CoreStateEnvelope::put32(b,uint32_t(v.tra_register_data));
   CoreStateEnvelope::put32(b,uint32_t(v.tra_flags));
   CoreStateEnvelope::put32(b,uint32_t(v.tra_bit_count_transmitted));
   CoreStateEnvelope::put32(b,uint32_t(v.tra_bit_count));
   if(!CoreTime::put(b,v.rcv_rate.m_seconds))return false;
   if(!CoreTime::put(b,v.tra_rate.m_seconds))return false;
   CoreStateEnvelope::put32(b,uint32_t(v.rcv_line));
   CoreStateEnvelope::put32(b,uint32_t(v.tra_clock_state));
   CoreStateEnvelope::put32(b,uint32_t(v.rcv_clock_state));
  }
  return CoreStateEnvelope::encode(b,out);
 }
 static bool decode(uint8_t const* p,size_t n,State& out){
  std::vector<uint8_t> b;
  if(!CoreStateEnvelope::decode(p,n,b)||b.size()!=228||CoreStateEnvelope::get32(b.data())!=tag)return false;
  size_t i=4;State s{};
  for(auto& v:s){
   {auto x=CoreStateEnvelope::get32(b.data()+i);i+=4;if(x>1u)return false;v.start_bit_hack_for_external_clocks=static_cast<bool>(x);}
   {auto x=CoreStateEnvelope::get32(b.data()+i);i+=4;if(x>2147483647u)return false;v.df_start_bit_count=static_cast<int>(x);}
   {auto x=CoreStateEnvelope::get32(b.data()+i);i+=4;if(x>255u)return false;v.df_word_length=static_cast<uint8_t>(x);}
   {auto x=CoreStateEnvelope::get32(b.data()+i);i+=4;if(x>255u)return false;v.df_parity=static_cast<uint8_t>(x);}
   {auto x=CoreStateEnvelope::get32(b.data()+i);i+=4;if(x>255u)return false;v.df_stop_bit_count=static_cast<uint8_t>(x);}
   {auto x=CoreStateEnvelope::get32(b.data()+i);i+=4;if(x>255u)return false;v.df_min_rx_stop_bit_count=static_cast<uint8_t>(x);}
   {auto x=CoreStateEnvelope::get32(b.data()+i);i+=4;if(x>65535u)return false;v.rcv_register_data=static_cast<uint16_t>(x);}
   {auto x=CoreStateEnvelope::get32(b.data()+i);i+=4;if(x>255u)return false;v.rcv_flags=static_cast<uint8_t>(x);}
   {auto x=CoreStateEnvelope::get32(b.data()+i);i+=4;if(x>255u)return false;v.rcv_bit_count_received=static_cast<uint8_t>(x);}
   {auto x=CoreStateEnvelope::get32(b.data()+i);i+=4;if(x>255u)return false;v.rcv_bit_count=static_cast<uint8_t>(x);}
   {auto x=CoreStateEnvelope::get32(b.data()+i);i+=4;if(x>255u)return false;v.rcv_byte_received=static_cast<uint8_t>(x);}
   {auto x=CoreStateEnvelope::get32(b.data()+i);i+=4;if(x>1u)return false;v.rcv_framing_error=static_cast<bool>(x);}
   {auto x=CoreStateEnvelope::get32(b.data()+i);i+=4;if(x>1u)return false;v.rcv_parity_error=static_cast<bool>(x);}
   {auto x=CoreStateEnvelope::get32(b.data()+i);i+=4;if(x>65535u)return false;v.tra_register_data=static_cast<uint16_t>(x);}
   {auto x=CoreStateEnvelope::get32(b.data()+i);i+=4;if(x>255u)return false;v.tra_flags=static_cast<uint8_t>(x);}
   {auto x=CoreStateEnvelope::get32(b.data()+i);i+=4;if(x>255u)return false;v.tra_bit_count_transmitted=static_cast<uint8_t>(x);}
   {auto x=CoreStateEnvelope::get32(b.data()+i);i+=4;if(x>255u)return false;v.tra_bit_count=static_cast<uint8_t>(x);}
   if(!CoreTime::get(b.data()+i,v.rcv_rate.m_seconds))return false;i+=16;
   if(!CoreTime::get(b.data()+i,v.tra_rate.m_seconds))return false;i+=16;
   {auto x=CoreStateEnvelope::get32(b.data()+i);i+=4;if(x>255u)return false;v.rcv_line=static_cast<uint8_t>(x);}
   {auto x=CoreStateEnvelope::get32(b.data()+i);i+=4;if(x>2147483647u)return false;v.tra_clock_state=static_cast<int>(x);}
   {auto x=CoreStateEnvelope::get32(b.data()+i);i+=4;if(x>2147483647u)return false;v.rcv_clock_state=static_cast<int>(x);}
   if(!device_serial_interface::valid_serial_recall(v))return false;
  }
  out=std::move(s);return true;
 }
};
}
