#pragma once
#include "core_state_envelope.h"
#include <array>
#include <vector>
#include <cstdint>
namespace eiii::vst3 {
struct CoreSccFifo {
 struct Channel {
  std::array<int32_t,6> cursors{};
  std::array<uint8_t,9> rx_data{},rx_errors{};
  std::array<uint8_t,4> tx_data{},tx_errors{};
 };
 struct State {Channel a,b;};
 static constexpr uint32_t tag=0x31464653u;
 static bool valid(Channel const& c) {
  auto const& v=c.cursors;
  return v[2]>=2&&v[2]<=9&&v[5]>=1&&v[5]<=4&&
         v[0]>=0&&v[0]<v[2]&&v[1]>=0&&v[1]<v[2]&&
         v[3]>=0&&v[3]<v[5]&&v[4]>=0&&v[4]<v[5];
 }
 static bool encode(State const& s,std::vector<uint8_t>& out) {
  if(!valid(s.a)||!valid(s.b))return false;
  std::vector<uint8_t> p,w;CoreStateEnvelope::put32(p,tag);
  for(auto const* c:{&s.a,&s.b}){
   for(auto x:c->cursors)CoreStateEnvelope::put32(p,uint32_t(x));
   for(auto x:c->rx_data)p.push_back(x);
   for(auto x:c->rx_errors)p.push_back(x);
   for(auto x:c->tx_data)p.push_back(x);
   for(auto x:c->tx_errors)p.push_back(x);
  }
  if(!CoreStateEnvelope::encode(p,w))return false;
  out.swap(w);return true;
 }
 static bool decode(uint8_t const* data,size_t size,State& out){
  std::vector<uint8_t> p;
  if(!CoreStateEnvelope::decode(data,size,p)||p.size()!=104||
     CoreStateEnvelope::get32(p.data())!=tag)return false;
  State s;size_t i=4;
  for(auto* c:{&s.a,&s.b}){
   for(auto& x:c->cursors){x=int32_t(CoreStateEnvelope::get32(p.data()+i));i+=4;}
   for(auto& x:c->rx_data)x=p[i++];
   for(auto& x:c->rx_errors)x=p[i++];
   for(auto& x:c->tx_data)x=p[i++];
   for(auto& x:c->tx_errors)x=p[i++];
   if(!valid(*c))return false;
  }
  out=s;return true;
 }
};
}
