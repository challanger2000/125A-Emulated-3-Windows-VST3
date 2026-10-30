#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include "../vst3-port/core_cd_section.h"
int main(){
 using C=eiii::vst3::CoreCdSection;
 C::State s;s.sector[0]=0x91;s.mode[2]=0x13;
 s.transfer={10,20,30,40};s.transfer_position=2;
 s.media_present=true;s.media_length=0x123456789ULL;
 s.media_fingerprint=0xFEDCBA9876543210ULL;s.lba=42;
 s.cached_sector=40;s.removal_prevented=true;s.sotc=1;
 std::vector<uint8_t> bytes;assert(C::encode(s,bytes));
 C::State out;assert(C::decode(bytes.data(),bytes.size(),out));
 assert(out.sector==s.sector&&out.mode==s.mode);
 assert(out.transfer==s.transfer&&out.transfer_position==2);
 assert(out.media_fingerprint==s.media_fingerprint);
 bytes.back()^=1;
 assert(!C::decode(bytes.data(),bytes.size(),out));
 assert(out.media_fingerprint==s.media_fingerprint);
 C::State empty;std::vector<uint8_t> emptyBytes;
 assert(C::encode(empty,emptyBytes));
 assert(C::decode(emptyBytes.data(),emptyBytes.size(),out));
 assert(out.transfer.empty()&&out.transfer_position==0);
 C::State bad=s;bad.transfer_position=5;
 assert(!C::encode(bad,bytes));
}
