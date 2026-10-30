#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include "../vst3-port/core_motherboard_pit.h"
int main() {
 using P=eiii::vst3::CoreMotherboardPit;
 P::State s{},result{};
 s[0].clock_hz=1000000;s[0].last_t=0.125;
 s[0].mode=3;s[0].rw=2;s[0].reload=65536;
 s[0].flags[2]=true;s[0].rem=12345;
 std::vector<uint8_t> bytes;
 assert(P::encode(s,bytes));
 assert(P::decode(bytes.data(),bytes.size(),result));
 assert(result[0].clock_hz==1000000&&result[0].mode==3);
 assert(result[0].flags[2]&&result[0].rem==12345);
 assert(result[0].reload==65536&&result[0].rw==2);
 assert(result[1].mode==0&&result[2].mode==0);
 bytes.back()^=1;
 assert(!P::decode(bytes.data(),bytes.size(),result));
 assert(result[0].rem==12345);
}
