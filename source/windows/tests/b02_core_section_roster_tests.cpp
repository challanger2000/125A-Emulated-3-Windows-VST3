#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include "../vst3-port/core_section_roster.h"
int main(){
 using C=eiii::vst3::CoreSectionContainer;
 using R=eiii::vst3::CoreSectionRoster;
 std::vector<C::Section> sections;
 for(uint32_t i=1;i<=23;++i)sections.push_back({i,{1}});
 std::array<bool,8> disk{},cd{};
 assert(R::valid(sections,disk,cd));
 disk[1]=true;sections.push_back({33,{1}});sections.push_back({34,{1}});
 assert(R::valid(sections,disk,cd));
 disk[1]=false;cd[1]=true;
 assert(!R::valid(sections,disk,cd));
 // All twenty-three fixed machine-state sections are mandatory.
 sections.back().id=35;
 assert(R::valid(sections,disk,cd));
 disk[1]=true;
 assert(!R::valid(sections,disk,cd));
 // Prevent conflicting SCSI media topology before emitting a checkpoint.
 disk[1]=false;cd[1]=false;
 assert(!R::valid(sections,disk,cd));
 sections.resize(23);
 assert(R::valid(sections,disk,cd));
 sections.pop_back();
 assert(!R::valid(sections,disk,cd));
}
