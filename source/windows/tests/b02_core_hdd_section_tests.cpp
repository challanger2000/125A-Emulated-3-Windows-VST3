#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include "../vst3-port/core_hdd_section.h"
int main() {
 using C=eiii::vst3::CoreHddSection;
 C::State s;s.sector[0]=0x91;s.sector[511]=0x28;s.lba=123;s.cur_lba=122;
 s.inquiry={1,2,3};s.model_name="EIII Disk";
 s.media_present=true;s.media_length=987654321ULL;
 s.media_fingerprint=0xABCDEF1234567890ULL;
 s.seek_model=true;s.last_cylinder=9;s.interleave=2;s.rpm=5400;
 s.seek_track_us=700;s.seek_range_us=22000;s.seek_exponent=1.75;
 std::vector<uint8_t> bytes;assert(C::encode(s,bytes));
 C::State restored;assert(C::decode(bytes.data(),bytes.size(),restored));
 assert(restored.sector==s.sector&&restored.lba==123);
 assert(restored.inquiry==s.inquiry&&restored.model_name==s.model_name);
 assert(restored.media_fingerprint==s.media_fingerprint);
 assert(restored.seek_exponent==s.seek_exponent&&restored.rpm==s.rpm);
 bytes.back()^=1;
 assert(!C::decode(bytes.data(),bytes.size(),restored));
 assert(restored.sector[0]==0x91&&restored.media_length==s.media_length);
 C::State empty;std::vector<uint8_t> emptyWire;
 assert(C::encode(empty,emptyWire));
 assert(C::decode(emptyWire.data(),emptyWire.size(),restored));
 assert(restored.model_name.empty()&&restored.inquiry.empty());
 // Confirmed both populated and empty HDD metadata wire layouts.
}
