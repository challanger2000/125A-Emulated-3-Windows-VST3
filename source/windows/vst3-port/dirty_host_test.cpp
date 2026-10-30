// A10: real, packaged VST3 controller -> IComponentHandler2 test.
// No original EIII ROM or OS media needed.
#define NOMINMAX
#include <windows.h>
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include <atomic>
#include <cstdio>
#include <cstring>
#include <chrono>
#include <stdexcept>

using namespace Steinberg;
using namespace Steinberg::Vst;
static void check(bool ok,const char* reason){if(!ok)throw std::runtime_error(reason);}
struct HostDirtyHandler final : IComponentHandler, IComponentHandler2 {
 std::atomic<uint32> refs{1};
 unsigned dirtyCalls=0;
 unsigned rejectedCalls=0;
 bool allowDirty=true;
 DWORD ownerThread=GetCurrentThreadId();
 tresult PLUGIN_API queryInterface(const TUID iid,void** out) override {
  if(!out)return kInvalidArgument;
  if(FUnknownPrivate::iidEqual(iid,IComponentHandler2::iid)){
   *out=static_cast<IComponentHandler2*>(this);addRef();return kResultOk;
  }
  if(FUnknownPrivate::iidEqual(iid,IComponentHandler::iid)||
     FUnknownPrivate::iidEqual(iid,FUnknown::iid)){
   *out=static_cast<IComponentHandler*>(this);addRef();return kResultOk;
  }
  *out=nullptr;return kNoInterface;
 }
 uint32 PLUGIN_API addRef() override{return ++refs;}
 uint32 PLUGIN_API release() override{return --refs;}
 tresult PLUGIN_API beginEdit(ParamID) override{return kResultOk;}
 tresult PLUGIN_API performEdit(ParamID,ParamValue) override{return kResultOk;}
 tresult PLUGIN_API endEdit(ParamID) override{return kResultOk;}
 tresult PLUGIN_API restartComponent(int32) override{return kResultOk;}
 tresult PLUGIN_API setDirty(TBool state) override {
  check(state!=0,"A10 host received setDirty(false) unexpectedly");
  check(GetCurrentThreadId()==ownerThread,"R04 setDirty called outside UI thread");
  if(!allowDirty) {++rejectedCalls;return kResultFalse;}
  ++dirtyCalls;return kResultOk;
 }
 tresult PLUGIN_API requestOpenEditor(FIDString) override{return kResultOk;}
 tresult PLUGIN_API startGroupEdit() override{return kResultOk;}
 tresult PLUGIN_API finishGroupEdit() override{return kResultOk;}
};
int wmain(int argc,wchar_t** argv){
 try {
  check(argc==2,"usage: eiii_dirty_host_test <packaged plugin binary>");
  HMODULE module=LoadLibraryW(argv[1]);
  check(module!=nullptr,"load VST3 module");
  auto* factory=reinterpret_cast<IPluginFactory*(*)()>(GetProcAddress(module,"GetPluginFactory"))();
  check(factory!=nullptr,"get factory");
  PClassInfo info{};
  check(factory->getClassInfo(0,&info)==kResultOk,"processor class");
  HostApplication host;
  IComponent* component=nullptr;
  check(factory->createInstance(info.cid,IComponent::iid,reinterpret_cast<void**>(&component))==kResultOk,"create component");
  check(component->initialize(&host)==kResultOk,"init component");
  check(FindWindowExW(HWND_MESSAGE,nullptr,L"STATIC",L"125A EIII Dirty Pump")!=nullptr,
        "R04 headless timer must exist before creating any editor");
  TUID controllerId{};
  check(component->getControllerClassId(controllerId)==kResultOk,"controller ID");
  IEditController* controller=nullptr;
  check(factory->createInstance(controllerId,IEditController::iid,reinterpret_cast<void**>(&controller))==kResultOk,"create controller");
  check(controller->initialize(&host)==kResultOk,"init controller");
  HostDirtyHandler handler;
  check(controller->setComponentHandler(&handler)==kResultOk,"attach dirty host handler");
  IConnectionPoint* componentPoint=nullptr,*controllerPoint=nullptr;
  check(component->queryInterface(IConnectionPoint::iid,reinterpret_cast<void**>(&componentPoint))==kResultOk,"component point");
  check(controller->queryInterface(IConnectionPoint::iid,reinterpret_cast<void**>(&controllerPoint))==kResultOk,"controller point");
  check(componentPoint->connect(controllerPoint)==kResultOk,"processor to controller connection");
  check(controllerPoint->connect(componentPoint)==kResultOk,"controller to processor connection");

  HostMessage poll;poll.setMessageID("EIII.GUI.Poll");
  check(componentPoint->notify(&poll)==kResultOk,"initial poll");
  check(handler.dirtyCalls==0,"initial poll dirtied project");
  HostMessage recall;recall.setMessageID("EIII.GUI.AutoRecall");
  recall.getAttributes()->setInt("bank",4);
  recall.getAttributes()->setInt("preset",-1);
  check(componentPoint->notify(&recall)==kResultOk,"accepted recall");
  check(handler.dirtyCalls==1,"accepted recall did not mark host dirty exactly once");
  check(componentPoint->notify(&recall)==kResultOk,"duplicate recall");
  check(handler.dirtyCalls==1,"duplicate recall marked dirty");
  recall.getAttributes()->setInt("bank",104);
  check(componentPoint->notify(&recall)!=kResultOk,"invalid recall accepted");
  check(handler.dirtyCalls==1,"rejected recall dirtied host");
  check(componentPoint->notify(&poll)==kResultOk,"poll after recall");
  check(handler.dirtyCalls==1,"poll resent already delivered revision");

  HostMessage route;route.setMessageID("EIII.GUI.MidiRouting");
  route.getAttributes()->setInt("target",13);
  check(componentPoint->notify(&route)==kResultOk,"accepted routing");
  check(handler.dirtyCalls==2,"changed MIDI routing not dirty");
  check(componentPoint->notify(&route)==kResultOk,"duplicate routing");
  check(handler.dirtyCalls==2,"duplicate routing dirtied host");
  route.getAttributes()->setInt("target",18);
  check(componentPoint->notify(&route)!=kResultOk,"invalid routing accepted");
  check(handler.dirtyCalls==2,"invalid routing dirtied host");

  HostMessage wavCancel;wavCancel.setMessageID("EIII.GUI.WavCancel");
  check(componentPoint->notify(&wavCancel)==kResultOk,"WAV cancel");
  check(componentPoint->notify(&poll)==kResultOk,"final poll");
  check(handler.dirtyCalls==2,"cancel or idle poll dirtied host");

  // R04: no editor ever existed. A transient host refusal cannot permanently
  // lose the project dirty notice. Only the UI-thread message loop may retry.
  handler.allowDirty=false;
  recall.getAttributes()->setInt("bank",5);
  check(componentPoint->notify(&recall)==kResultOk,"R04 changed recall while handler refuses");
  check(handler.dirtyCalls==2 && handler.rejectedCalls>=1,
        "R04 first rejected dirty delivery must remain pending");
  handler.allowDirty=true;
  const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
  while(handler.dirtyCalls==2 && std::chrono::steady_clock::now()<deadline) {
   MSG msg{};
   while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)) {
    TranslateMessage(&msg);DispatchMessageW(&msg);
   }
   Sleep(4);
  }
  check(handler.dirtyCalls==3,
        "R04 headless UI pump failed to redeliver after disconnected/refusing handler");
  check(handler.rejectedCalls>=1,"R04 rejection not exercised");
  const auto quietUntil=std::chrono::steady_clock::now()+std::chrono::milliseconds(450);
  while(std::chrono::steady_clock::now()<quietUntil) {
   MSG msg{};
   while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)) {
    TranslateMessage(&msg);DispatchMessageW(&msg);
   }
   Sleep(4);
  }
  check(handler.dirtyCalls==3,"R04 acknowledged revision delivered repeatedly");

  check(controllerPoint->disconnect(componentPoint)==kResultOk,"disconnect controller");
  check(componentPoint->disconnect(controllerPoint)==kResultOk,"disconnect component");
  controllerPoint->release();componentPoint->release();
  controller->terminate();controller->release();
  component->terminate();component->release();
  check(FindWindowExW(HWND_MESSAGE,nullptr,L"STATIC",L"125A EIII Dirty Pump")==nullptr,
        "R04 no callback-bearing HWND after processor termination");
  factory->release();
  const auto shutdown=reinterpret_cast<bool(*)()>(GetProcAddress(module,"ExitDll"));
  if(shutdown)shutdown();
  FreeLibrary(module);
  std::puts("PASS A10/R04: packaged VST3 UI-thread dirty ACK/retry without editor, duplicate rejection, unload cleanup");
  return 0;
 }catch(const std::exception& ex){
  std::fprintf(stderr,"FAIL A10: %s\n",ex.what());
  return 1;
 }
}
