#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include "../vst3-port/core_section_container.h"
int main(){
 using C=eiii::vst3::CoreSectionContainer;
 std::vector<C::Section> src={{1,{1,2,3}},{3,{4,5}},{9,{6}}},out;
 std::vector<uint8_t> bytes;
 assert(C::encode(src,bytes)&&C::decode(bytes.data(),bytes.size(),out));
 assert(out.size()==3&&out[1].id==3&&out[1].bytes[1]==5);
 bytes.back()^=1;
 assert(!C::decode(bytes.data(),bytes.size(),out)&&out.size()==3);
 assert(!C::encode({{3,{1}},{3,{2}}},bytes));
 assert(!C::encode({{5,{1}},{4,{2}}},bytes));
 assert(!C::encode({{1,{}}},bytes));
 // Regression gate: reject missing, duplicate or reordered device sections.
}
