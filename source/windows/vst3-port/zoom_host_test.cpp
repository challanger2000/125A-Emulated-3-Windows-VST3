// A12: verify actual VST3 controller state, six attached HWND zoom sizes,
// rejected host resize, legacy EIII-UI-1 migration and reopening.
// The shipped VST3 binary is loaded; no private firmware or samples.
#define NOMINMAX
#include <windows.h>
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include <atomic>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
#include <algorithm>
using namespace Steinberg;
using namespace Steinberg::Vst;
static void must(bool value,const char* message){if(!value)throw std::runtime_error(message);}
struct Stream : IBStream {
 std::atomic<uint32> refs{1};std::vector<char> bytes;int64 pos=0;
 tresult PLUGIN_API queryInterface(const TUID iid,void** out) override {
  if(!out)return kInvalidArgument;
  if(FUnknownPrivate::iidEqual(iid,IBStream::iid)||
     FUnknownPrivate::iidEqual(iid,FUnknown::iid)){
   *out=static_cast<IBStream*>(this);addRef();return kResultOk;
  }*out=nullptr;return kNoInterface;
 }
 uint32 PLUGIN_API addRef() override{return ++refs;}
 uint32 PLUGIN_API release() override{return --refs;}
 tresult PLUGIN_API read(void* p,int32 n,int32* done) override {
  if(n<0||pos<0||uint64(pos)>bytes.size())return kInvalidArgument;
  size_t count=std::min<size_t>(size_t(n),bytes.size()-size_t(pos));
  if(count)memcpy(p,bytes.data()+pos,count);
  pos+=int64(count);if(done)*done=int32(count);
  return count?kResultOk:kResultFalse;
 }
 tresult PLUGIN_API write(void* p,int32 n,int32* done) override {
  if(n<0||pos<0)return kInvalidArgument;
  if(uint64(pos)+uint64(n)>bytes.size())bytes.resize(size_t(pos)+size_t(n));
  if(n)memcpy(bytes.data()+pos,p,size_t(n));
  pos+=n;if(done)*done=n;
  return kResultOk;
 }
 tresult PLUGIN_API seek(int64 offset,int32 mode,int64* out) override {
  const int64 next=mode==0?offset:mode==1?pos+offset:int64(bytes.size())+offset;
  if(next<0||next>int64(bytes.size()))return kInvalidArgument;
  pos=next;if(out)*out=pos;return kResultOk;
 }
 tresult PLUGIN_API tell(int64* out) override {
  if(!out)return kInvalidArgument;*out=pos;return kResultOk;
 }
 void assign(const std::string& value){bytes.assign(value.begin(),value.end());pos=0;}
 std::string text() const{return std::string(bytes.begin(),bytes.end());}
};
struct Frame final : IPlugFrame {
 std::atomic<uint32> refs{1};
 bool accept=true;int calls=0;
 tresult PLUGIN_API queryInterface(const TUID iid,void** out) override {
  if(!out)return kInvalidArgument;
  if(FUnknownPrivate::iidEqual(iid,IPlugFrame::iid)||
     FUnknownPrivate::iidEqual(iid,FUnknown::iid)){
   *out=static_cast<IPlugFrame*>(this);addRef();return kResultOk;
  }*out=nullptr;return kNoInterface;
 }
 uint32 PLUGIN_API addRef() override{return ++refs;}
 uint32 PLUGIN_API release() override{return --refs;}
 tresult PLUGIN_API resizeView(IPlugView* view,ViewRect* size) override {
  ++calls;
  if(!accept)return kResultFalse;
  return view->onSize(size);
 }
};
static constexpr wchar_t rackClass[] = L"125A_EMULATED3_ORIGINAL_RACK_V1";
static void assertRackClass(HMODULE plugin, bool registered){
 WNDCLASSEXW wc{};wc.cbSize=sizeof(wc);
 const bool found=GetClassInfoExW(plugin,rackClass,&wc)!=0;
 must(found==registered,registered?"R01 rack class not registered":"R01 stale registered rack class");
 if(found)must(wc.hInstance==plugin,"R01 rack class belongs to host, not plugin");
}
static constexpr int width(int z){return (1472*z+50)/100;}
static constexpr int height(int z){return (538*z+50)/100+36;}
static void checkSize(IPlugView* view,int zoom) {
 ViewRect r{};
 must(view->getSize(&r)==kResultTrue,"view getSize");
 if(r.getWidth()!=width(zoom)||r.getHeight()!=height(zoom))
  throw std::runtime_error("restored zoom/geometry mismatch");
}
static void save(IEditController* c,std::string& text){
 Stream s;must(c->getState(&s)==kResultOk,"controller getState");text=s.text();
}
static void restore(IEditController* c,const std::string& text,bool ok=true){
 Stream s;s.assign(text);
 must((c->setState(&s)==kResultOk)==ok,"controller setState validation");
}
static const char* oldTheme=
 "lcd_theme blue\nlcd_level stock 0.5500\nlcd_level blue 0.7000\n"
 "lcd_level amber 0.7000\nlcd_level vfd 0.6000\nlcd_level oled 0.8000\n";
int wmain(int argc,wchar_t** argv){
 try{
  must(argc==2,"usage eiii_zoom_host_test <packaged VST3 module>");
  HMODULE lib=LoadLibraryW(argv[1]);must(lib!=nullptr,"LoadLibrary");
  auto get=reinterpret_cast<IPluginFactory*(*)()>(GetProcAddress(lib,"GetPluginFactory"));
  must(get!=nullptr,"VST3 factory export");
  IPluginFactory* factory=get();must(factory!=nullptr,"VST3 factory");
  PClassInfo cls{};must(factory->getClassInfo(0,&cls)==kResultOk,"class info");
  HostApplication host;
  IComponent* component=nullptr;
  must(factory->createInstance(cls.cid,IComponent::iid,reinterpret_cast<void**>(&component))==kResultOk,"component create");
  must(component->initialize(&host)==kResultOk,"component initialize");
  TUID cid{};
  must(component->getControllerClassId(cid)==kResultOk,"controller uid");
  IEditController* controller=nullptr;
  must(factory->createInstance(cid,IEditController::iid,reinterpret_cast<void**>(&controller))==kResultOk,"controller create");
  must(controller->initialize(&host)==kResultOk,"controller initialize");
  HWND parent=CreateWindowExW(0,L"STATIC",L"A12-host",WS_OVERLAPPEDWINDOW,
      0,0,2500,1100,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
  must(parent!=nullptr,"test HWND parent");
  Frame frame;
  std::string stored;
  for(int zoom=100;zoom<=150;zoom+=10){
   const std::string text="EIII-UI-2\nzoom_percent "+std::to_string(zoom)+"\n";
   restore(controller,text);
   auto* view=controller->createView(ViewType::kEditor);
   must(view!=nullptr,"view creation");
   checkSize(view,zoom); // before any HWND exists, including 150%
   must(view->setFrame(&frame)==kResultOk,"setFrame");
   must(view->attached(parent,kPlatformTypeHWND)==kResultTrue,"HWND attached");
   assertRackClass(lib,true);
   checkSize(view,zoom);
   ViewRect size{};
   must(view->getSize(&size)==kResultTrue && view->onSize(&size)==kResultTrue,"host size attach");
   checkSize(view,zoom);
   save(controller,stored);
   must(stored.rfind("EIII-UI-2\nzoom_percent "+std::to_string(zoom)+"\n",0)==0,
       "controller persists chosen zoom");
   // Host declines loading another state's zoom into an existing view:
   // controller keeps desired saved zoom, HWND stays at previous accepted size.
   frame.accept=false;
   const int rejectedZoom=zoom==150?100:150;
   restore(controller,"EIII-UI-2\nzoom_percent "+std::to_string(rejectedZoom)+"\n");
   checkSize(view,zoom);
   save(controller,stored);
   must(stored.find("zoom_percent "+std::to_string(rejectedZoom)+"\n")!=std::string::npos,
        "failed restore retains desired state for next view");
   frame.accept=true;
   // Reaccept via a host callback and ensure restored state size matches.
   restore(controller,text);
   checkSize(view,zoom);
   IPlugView* other=nullptr;
   if(zoom==130){
    // R01: two editors share one module-scoped class; closing the first
    // must not invalidate the second HWND's WndProc.
    other=controller->createView(ViewType::kEditor);
    must(other!=nullptr,"R01 second view");
    must(other->setFrame(&frame)==kResultOk,"R01 second frame");
    must(other->attached(parent,kPlatformTypeHWND)==kResultTrue,"R01 second HWND");
    assertRackClass(lib,true);
   }
   must(view->removed()==kResultTrue,"remove HWND");
   view->release();
   assertRackClass(lib,other!=nullptr);
   if(other){
    must(other->removed()==kResultTrue,"R01 remove second HWND");
    other->release();
    assertRackClass(lib,false);
   }
   // A freshly opened view must reconstruct the last saved state, not 130%.
   auto* reopened=controller->createView(ViewType::kEditor);
   must(reopened!=nullptr,"reopen");
   checkSize(reopened,zoom);
   reopened->release();
  }
  // Historical controller-state files have no zoom; default to 130%.
  restore(controller,std::string("EIII-UI-1\n")+oldTheme);
  auto* legacy=controller->createView(ViewType::kEditor);
  must(legacy!=nullptr,"legacy view");checkSize(legacy,130);legacy->release();
  save(controller,stored);
  must(stored.rfind("EIII-UI-2\nzoom_percent 130\n",0)==0,
       "old state safely upgraded at save");
  // Parse rejection must be transactional (valid prior zoom remains intact).
  for(const auto& bad:{"EIII-UI-2\nzoom_percent 99\n",
       "EIII-UI-2\nzoom_percent 120\nzoom_percent 130\n",
       "EIII-UI-2\nzoom_percent 150 nonsense\n",
       "EIII-UI-2\nlcd_theme blue\n",
       "EIII-UI-1\nzoom_percent 150\n",
       "EIII-UI-3\nzoom_percent 130\n"}){
    restore(controller,bad,false);
    save(controller,stored);
    must(stored.rfind("EIII-UI-2\nzoom_percent 130\n",0)==0,
         "failed state parse changed persisted zoom");
  }
  DestroyWindow(parent);
  controller->terminate();controller->release();
  component->terminate();component->release();
  factory->release();
  const auto exit=reinterpret_cast<bool(*)()>(GetProcAddress(lib,"ExitDll"));
  if(exit)exit();
  assertRackClass(lib,false); // before unloading plugin DLL
  FreeLibrary(lib);
  // R01: plugin can be completely unloaded and loaded again without a stale
  // class pointing to the previous module/WndProc.
  HMODULE reloaded=LoadLibraryW(argv[1]);
  must(reloaded!=nullptr,"R01 plugin reload");
  assertRackClass(reloaded,false);
  auto get2=reinterpret_cast<IPluginFactory*(*)()>(GetProcAddress(reloaded,"GetPluginFactory"));
  must(get2!=nullptr,"R01 factory export after reload");
  IPluginFactory* factory2=get2();must(factory2!=nullptr,"R01 factory after reload");
  PClassInfo cls2{};must(factory2->getClassInfo(0,&cls2)==kResultOk,"R01 reloaded class info");
  HostApplication host2;
  IComponent* component2=nullptr;
  must(factory2->createInstance(cls2.cid,IComponent::iid,reinterpret_cast<void**>(&component2))==kResultOk,"R01 reloaded component");
  must(component2->initialize(&host2)==kResultOk,"R01 component initialize");
  TUID controllerId2{};must(component2->getControllerClassId(controllerId2)==kResultOk,"R01 controller ID");
  IEditController* controller2=nullptr;
  must(factory2->createInstance(controllerId2,IEditController::iid,reinterpret_cast<void**>(&controller2))==kResultOk,"R01 reload controller");
  must(controller2->initialize(&host2)==kResultOk,"R01 reload controller initialize");
  HWND parent2=CreateWindowExW(0,L"STATIC",L"R01-reload-host",WS_OVERLAPPEDWINDOW,
      0,0,2500,1100,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
  must(parent2!=nullptr,"R01 second parent");
  auto* reloadView=controller2->createView(ViewType::kEditor);
  must(reloadView!=nullptr,"R01 reloaded editor");
  Frame frame2;must(reloadView->setFrame(&frame2)==kResultOk,"R01 reloaded frame");
  must(reloadView->attached(parent2,kPlatformTypeHWND)==kResultTrue,"R01 attach after DLL reload");
  assertRackClass(reloaded,true);
  must(reloadView->removed()==kResultTrue,"R01 remove reloaded editor");
  reloadView->release();assertRackClass(reloaded,false);
  DestroyWindow(parent2);
  controller2->terminate();controller2->release();
  component2->terminate();component2->release();
  factory2->release();
  const auto exit2=reinterpret_cast<bool(*)()>(GetProcAddress(reloaded,"ExitDll"));
  if(exit2)exit2();
  FreeLibrary(reloaded);
  std::puts("PASS A12/R01: six attached HWND sizes, save/reopen, rejected resize, legacy migration and bad state");
  return 0;
 }catch(const std::exception& e){std::fprintf(stderr,"FAIL A12: %s\n",e.what());return 1;}
}
