// A14: release-active assertions for the exact production MIDI controller
// defaults and the existing 14-bit pitchbend decoder (no firmware needed).
#include "../vst3-port/midi_controllers.h"
#include "../vst3-port/midi_dispatch.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <vector>
struct Packet {std::array<uint8_t,3> data; unsigned length;};
static void require(bool value,const char* error){if(!value)throw std::runtime_error(error);}
int main() {
 using eiii::vst3::MidiControllers;
 using eiii::vst3::MidiRouting;
 using eiii::vst3::NoteOffRecovery;
 require(MidiControllers::base==2000 && MidiControllers::per_channel==4 &&
         MidiControllers::total==64,"A14: previously shipped IDs changed");
 for(unsigned ch=0;ch<16;++ch){
  for(unsigned slot=0;slot<4;++slot){
   uint8_t midi[3]={0xff,0xff,0xff};
   const double def=MidiControllers::default_normalized(slot);
   require(def==(slot==2?0.5:0.0),"A14: wrong slot default");
   require(MidiControllers::decode(MidiControllers::id(ch,slot),def,midi),
           "A14: controller default not decodable");
   require((midi[0]&15)==ch,"A14: default changed source MIDI channel");
   if(slot==0)
    require(midi[0]==(0xb0|ch)&&midi[1]==1&&midi[2]==0,
            "A14: modulation must remain zero");
   if(slot==1)
    require(midi[0]==(0xb0|ch)&&midi[1]==64&&midi[2]==0,
            "A14: sustain must remain OFF");
   if(slot==2)
    require(midi[0]==(0xe0|ch)&&midi[1]==0&&midi[2]==64,
            "A14: neutral pitchbend must encode exactly as MIDI 0x00 0x40");
   if(slot==3)
    require(midi[0]==(0xd0|ch)&&midi[1]==0,
            "A14: channel aftertouch must remain zero");
  }
  // Old saved/automated explicit bends remain effective. Only the fresh
  // controller's declared default is corrected, not the MIDI decoder.
  uint8_t explicitLow[3]{}, explicitHigh[3]{};
  require(MidiControllers::decode(MidiControllers::id(ch,2),0.0,explicitLow) &&
          MidiControllers::decode(MidiControllers::id(ch,2),1.0,explicitHigh),
          "A14: old explicit bend endpoints not decoded");
  require(explicitLow[0]==(0xe0|ch)&&explicitLow[1]==0&&explicitLow[2]==0 &&
          explicitHigh[0]==(0xe0|ch)&&explicitHigh[1]==127&&explicitHigh[2]==127,
          "A14: explicit bend endpoints altered");
 }
 // Trace through the actual A08/A09 production dispatcher, including a
 // held note routed to firmware channel 14; no guesses about private OS.
 MidiRouting routing;
 NoteOffRecovery recovery;
 routing.set_target(14);
 require(routing.note_on(0,60)==13,"A14: held note did not route to channel 14");
 std::vector<Packet> delivered;
 auto output=[&](const uint8_t* data,unsigned n)noexcept {
  delivered.push_back({{{data[0],data[1],data[2]}},n});
  return true;
 };
 uint8_t neutral[3]{};
 require(MidiControllers::decode(MidiControllers::id(0,2),0.5,neutral),
         "A14: neutral decoder failed");
 eiii::vst3::forward_controller(routing,recovery,neutral,output);
 require(delivered.size()==1 && delivered[0].length==3 &&
         delivered[0].data==(std::array<uint8_t,3>{0xed,0,64}),
         "A14: default bend not forwarded neutrally to a held note");
 std::puts("PASS A14: 16x4 MIDI defaults, pitch center 8192, explicit bend extremes and routed MIDI bytes");
}
