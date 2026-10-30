// A14: real packaged Windows VST3 factory/controller MIDI mapping and
// defaultNormalizedValue audit. No original ROM/OS in public CI.
#define NOMINMAX
#include <windows.h>
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "midi_controllers.h"
#include "midi_dispatch.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <vector>
using namespace Steinberg;
using namespace Steinberg::Vst;
static void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
int wmain(int argc,wchar_t** argv){
 try {
  require(argc==2,"usage: eiii_pitchbend_host_test <VST3 module>");
  const HMODULE module=LoadLibraryW(argv[1]);
  require(module!=nullptr,"A14 cannot load actual VST3 module");
  const auto getFactory=reinterpret_cast<IPluginFactory*(*)()>(
       GetProcAddress(module,"GetPluginFactory"));
  require(getFactory!=nullptr,"A14 missing factory export");
  IPluginFactory* factory=getFactory();
  require(factory!=nullptr,"A14 factory null");
  PClassInfo info{};
  require(factory->getClassInfo(0,&info)==kResultOk,"A14 processor class");
  HostApplication host;
  IComponent* processor=nullptr;
  require(factory->createInstance(info.cid,IComponent::iid,
       reinterpret_cast<void**>(&processor))==kResultOk,"A14 processor instance");
  require(processor->initialize(&host)==kResultOk,"A14 processor init");
  TUID controllerId{};
  require(processor->getControllerClassId(controllerId)==kResultOk,"A14 controller FUID");
  IEditController* controller=nullptr;
  require(factory->createInstance(controllerId,IEditController::iid,
       reinterpret_cast<void**>(&controller))==kResultOk,"A14 controller instance");
  require(controller->initialize(&host)==kResultOk,"A14 controller initialize");
  IMidiMapping* mapping=nullptr;
  require(controller->queryInterface(IMidiMapping::iid,
       reinterpret_cast<void**>(&mapping))==kResultOk && mapping,
       "A14 controller does not expose IMidiMapping");
  require(controller->getParameterCount()==64,
       "A14 hidden MIDI parameter count or compatibility ID changed");
  const CtrlNumber controllers[4]={kCtrlModWheel,kCtrlSustainOnOff,kPitchBend,kAfterTouch};
  for(int index=0;index<controller->getParameterCount();++index){
    ParameterInfo param{};
    require(controller->getParameterInfo(index,param)==kResultOk,
            "A14 cannot query parameter metadata");
    const unsigned ch=unsigned(index)/4, slot=unsigned(index)%4;
    const ParamID id=eiii::vst3::MidiControllers::id(ch,slot);
    require(param.id==id,"A14 changed historical MIDI parameter IDs");
    require((param.flags&ParameterInfo::kIsHidden)!=0,
            "A14 MIDI parameter is no longer hidden");
    const double expected=slot==2?0.5:0.0;
    require(param.defaultNormalizedValue==expected,
            "A14 pitchbend/other parameter metadata default wrong");
    require(controller->getParamNormalized(id)==expected,
            "A14 live controller initialization differs from metadata");
    ParamID mapped=0;
    require(mapping->getMidiControllerAssignment(0,int16(ch),
       controllers[slot],mapped)==kResultTrue && mapped==id,
       "A14 host controller assignment changed");
    uint8_t midi[3]{};
    require(eiii::vst3::MidiControllers::decode(id,expected,midi),
            "A14 real mapped parameter not decodable");
    if(slot==2){
      require(midi[0]==(0xe0|ch)&&midi[1]==0&&midi[2]==64,
              "A14 mapped default must produce centered pitchbend");
      // Processing path shared with EIII processor and existing A08/A09
      // regression: ensure real mapped default reaches intended channel.
      eiii::vst3::MidiRouting route;
      eiii::vst3::NoteOffRecovery recovery;
      route.set_target(14);
      bool received=false;
      eiii::vst3::forward_controller(route,recovery,midi,
        [&](const uint8_t* bytes,unsigned n)noexcept{
          received=true;
          return n==3 && bytes[0]==0xed&&bytes[1]==0&&bytes[2]==64;
        });
      require(received,"A14 mapped default not passed to MIDI dispatcher");
      // Existing saved explicit negative/positive bend must remain unchanged.
      uint8_t oldLow[3]{},oldHigh[3]{};
      require(eiii::vst3::MidiControllers::decode(id,0.0,oldLow)&&
              eiii::vst3::MidiControllers::decode(id,1.0,oldHigh)&&
              oldLow[1]==0&&oldLow[2]==0&&oldHigh[1]==127&&oldHigh[2]==127,
              "A14 explicit stored pitchbend endpoints changed");
    }
  }
  mapping->release();
  controller->terminate();
  controller->release();
  processor->terminate();
  processor->release();
  factory->release();
  const auto shutdown=reinterpret_cast<bool(*)()>(GetProcAddress(module,"ExitDll"));
  if(shutdown)shutdown();
  FreeLibrary(module);
  std::puts("PASS A14: 64 actual VST3 parameters, 16 neutral pitchbend defaults, mapping and MIDI routing");
  return 0;
 } catch(const std::exception& ex) {
  std::fprintf(stderr,"FAIL A14: %s\n",ex.what());return 1;
 }
}
