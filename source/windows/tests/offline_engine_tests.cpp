// Real engine worker synchronization; no mocked producer or firmware writes.
#include "../../upstream/runtime/eiii_engine.h"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>
using namespace std::chrono_literals;
namespace {
void check(bool ok,const char* why) {if(!ok)throw std::runtime_error(why);}
template<class T> void ready(std::future<T>& f,const char* why) {
 if(f.wait_for(5s)!=std::future_status::ready){std::cerr<<"FAIL: "<<why<<std::endl;std::_Exit(1);}
}
}
int main() {
 try {
  eiii::engine engine;eiii::engine_config cfg;cfg.realtime=false;
  float one=1,two=1;engine.set_realtime(false);engine.pull(&one,&two,1);
  check(one==0&&two==0,"offline pull before start did not return silence");
  engine.start(cfg); // Missing ROM is deliberate: the real worker feeds silence.
  for(size_t frames:{size_t(0),size_t(1),size_t(64),size_t(257),size_t(1024),size_t(16385),size_t(65536)}) {
   std::vector<float> l(frames,1),r(frames,1);
   const auto before=engine.stats();
   auto pull=std::async(std::launch::async,[&]{engine.pull(l.data(),r.data(),frames);});
   ready(pull,"offline block deadlock");pull.get();
   check(std::all_of(l.begin(),l.end(),[](float v){return v==0;})&&
         std::all_of(r.begin(),r.end(),[](float v){return v==0;}),"invalid unpowered audio");
   const auto after=engine.stats();
   check(after.frames_out-before.frames_out==frames,"offline frames missing");
   check(after.underrun_frames==before.underrun_frames,"offline underrun");
  }
  engine.shutdown();engine.shutdown();
  // Missing-media boot and 30 stop/start cycles must remain bounded.
  for(unsigned i=0;i<30;++i){engine.start(cfg);engine.pull(&one,&two,1);engine.shutdown();}
  std::cout<<"PASS offline engine: zero/small/large blocks, missing media, 30 restarts\n";
  const char* rom=std::getenv("EIII_ROM_DIR"),*hd=std::getenv("EIII_HD_IMAGE");
  if(!rom||!hd){std::cout<<"SKIP firmware-dependent blocked-worker lifecycle cases\n";return 77;}
  cfg.rt.rom_dir=rom;cfg.rt.hd_path=hd;
  for(unsigned action=0;action<4;++action) {
   engine.start(cfg);
   // Opening the real machine happens on its worker; inspect is ordered after boot.
   std::promise<void> entered,release;auto in=entered.get_future();auto go=release.get_future().share();
   check(engine.inspect([&](eiii::runtime&){entered.set_value();go.wait();if(action==2)throw std::runtime_error("intentional worker failure");}),"inspect rejected");
   ready(in,"worker did not enter inspect");in.get();
   std::vector<float> l(65536),r(65536);
   auto pull=std::async(std::launch::async,[&]{engine.pull(l.data(),r.data(),l.size());});
   // No production delay: wait_for proves the reader is blocked by the held worker.
   check(pull.wait_for(20ms)==std::future_status::timeout,"offline reader did not wait for producer");
   if(action==0) {
    engine.set_realtime(true);ready(pull,"mode switch did not wake reader");pull.get();release.set_value();engine.shutdown();
   } else if(action==1) {
    auto stop=std::async(std::launch::async,[&]{engine.shutdown();});
    ready(pull,"shutdown did not wake reader");pull.get();release.set_value();ready(stop,"shutdown did not join");stop.get();
   } else if(action==2) {
    release.set_value();ready(pull,"worker failure did not wake reader");pull.get();engine.shutdown();
   } else {
    check(pull.wait_for(250ms)==std::future_status::timeout,"held worker unexpectedly progressed");
    const auto before=engine.stats();release.set_value();ready(pull,"slow producer deadlock");pull.get();
    check(engine.stats().underrun_frames==before.underrun_frames,"slow offline producer caused underrun");
    engine.shutdown();
   }
   eiii::media_result result;while(engine.poll_media_result(result)){}
  }
  std::cout<<"PASS offline engine: blocked producer, mode switch, shutdown, exception wakeups, >200ms producer stall\n";return 0;
 }catch(const std::exception& e){std::cerr<<"FAIL offline engine: "<<e.what()<<std::endl;return 1;}
}
