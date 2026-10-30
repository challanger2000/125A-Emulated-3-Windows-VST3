// Lossless, pointer-free time transport. Two binary64 values preserve the
// native Windows binary64 and Linux x87 (64-bit significand) clocks exactly.
// This changes serialization only, never the emulator's arithmetic or timing.
#pragma once
#include "core_state_envelope.h"
#include <cmath>
#include <cstring>
#include <limits>
namespace eiii::vst3 {
struct CoreTime {
 static_assert(std::numeric_limits<long double>::digits <= 106);
 static constexpr size_t bytes=16;
 static bool valid(long double t) {
  return std::isfinite(t) && t>=0 && t<=static_cast<long double>(1e30);
 }
 static void put_double(std::vector<uint8_t>& out,double t) {
  uint64_t bits;std::memcpy(&bits,&t,8);
  CoreStateEnvelope::put32(out,uint32_t(bits));
  CoreStateEnvelope::put32(out,uint32_t(bits>>32));
 }
 static double get_double(uint8_t const* p) {
  uint64_t bits=CoreStateEnvelope::get32(p)|uint64_t(CoreStateEnvelope::get32(p+4))<<32;
  double t;std::memcpy(&t,&bits,8);return t;
 }
 static bool put(std::vector<uint8_t>& out,long double t) {
  if(!valid(t))return false;
  double hi=double(t),lo=double(t-static_cast<long double>(hi));
  if(static_cast<long double>(hi)+static_cast<long double>(lo)!=t)return false;
  put_double(out,hi);put_double(out,lo);return true;
 }
 static bool get(uint8_t const* p,long double& out) {
  double hi=get_double(p),lo=get_double(p+8);
  if(!std::isfinite(hi)||!std::isfinite(lo))return false;
  long double t=static_cast<long double>(hi)+static_cast<long double>(lo);
  // Reject precision-losing cross-platform states and noncanonical pairs.
  if(!valid(t)||double(t)!=hi||double(t-static_cast<long double>(hi))!=lo)return false;
  out=t;return true;
 }
};
}
