#include "../vst3-port/eiii_zoom_state.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdio>
#include <string>
int main(){
 using namespace eiii::vst3;
 assert(zoom_default==130);
 for(int zoom=100;zoom<=150;zoom+=10){
   assert(valid_zoom(zoom));
   int actual=-1;
   assert(parse_zoom_line("zoom_percent "+std::to_string(zoom),actual));
   assert(actual==zoom);
   zoom_resize_contract state;
   state.set_initial(130);
   if(zoom!=130){
     assert(state.request(zoom,true));
     assert(state.selected()==130);
     // Host refusal, including an optimistic kResultTrue with no onSize:
     // the requested geometry must not be recorded.
     state.rejected();
     assert(state.selected()==130);
     assert(state.request(zoom,true));
     assert(state.host_sized(zoom));
     assert(state.selected()==zoom);
     assert(!state.request(zoom,true)); // unchanged zoom
     // A state-restore must apply the size without dirtying the host.
     state.set_initial(130);
     assert(state.request(zoom,false));
     assert(!state.host_sized(zoom));
     assert(state.selected()==zoom);
     // A stale/other resize is not an acknowledged user selection.
     state.set_initial(130);
     assert(state.request(zoom,true));
     const int other=zoom==100?110:100;
     assert(!state.host_sized(other));
     assert(state.selected()==other);
   }else{
     assert(!state.request(130,true));
     assert(!state.host_sized(130));
   }
 }
 for(int bad : {-1,0,20,99,101,109,151,160,1000}){
   assert(!valid_zoom(bad));
   zoom_resize_contract gate;
   assert(!gate.request(bad,true));
   assert(!gate.host_sized(bad));
   assert(gate.selected()==130);
 }
 for(const char* malformed: {"zoom_percent", "zoom_percent ", "zoom_percent 99",
      "zoom_percent 151","zoom_percent 105","zoom_percent -100",
      "zoom_percent 130 garbage","zoom_percent 130.0",
      "zoom_percent 0130","zoom_percent +130", "zoom_percent 130\r",
      "zoom_percent 9999999999999999999999999","zoom_percent\t130",
      "zoom_percent 130\nzoom_percent 140"}) {
   int out=170;
   if(std::string(malformed)=="zoom_percent 0130"){
     // from_chars intentionally accepts leading zeros; geometry remains valid.
     assert(parse_zoom_line(malformed,out)&&out==130);
   } else assert(!parse_zoom_line(malformed,out)&&out==170);
 }
 std::puts("PASS A12: six zoom steps, host-acknowledged edits, rejection, restore and malformed state");
}
