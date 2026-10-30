#pragma once
#include "core_section_container.h"
#include <array>
namespace eiii::vst3 {
struct CoreSectionRoster {
 static bool valid(std::vector<CoreSectionContainer::Section> const& sections,
                   std::array<bool,8> const& disks,
                   std::array<bool,8> const& cds,bool d0_present=false) {
  if(sections.size()<23)return false;
  for(size_t i=0;i<23;++i)if(sections[i].id!=i+1)return false;
  size_t index=23;
  for(unsigned id=0;id<8;++id){
   if(disks[id]&&cds[id])return false;
   if(!disks[id]&&!cds[id])continue;
   if(index+1>=sections.size()||
      sections[index].id!=30+3*id||
      sections[index+1].id!=30+3*id+(disks[id]?1:2))return false;
   index+=2;
  }
  // D0 is not a SCSI target. Its optional versioned controller/drive/PLL
  // section follows every D1/D3/D4..D7 section. Older no-D0 snapshots
  // remain valid, but a mounted D0 requires a complete D0 section.
  if(d0_present) {
   if(index>=sections.size()||sections[index].id!=60)return false;
   ++index;
  }
  return index==sections.size();
 }
};
}
