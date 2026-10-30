// Snapshot immediately after reset, before the first CPU memory transaction.
// Poison storage to expose reliance on allocator contents; no ROM or CPU patch.
#include "../../upstream/runtime/eiii_machine.h"
#include <cstring>
#include <iostream>
#include <memory>
#include <new>
int main(){
 machine_config config;
 auto memory=std::make_unique<eiii::machine_t>();
 auto* previous=device_memory_interface::s_host_space;
 device_memory_interface::s_host_space=memory.get();
 auto* storage=::operator new(sizeof(ns32016_device));
 std::memset(storage,0xa5,sizeof(ns32016_device));
 auto* cpu=::new(storage) ns32016_device(config,"cpu",nullptr,10000000);
 cpu->resolve();cpu->start();cpu->reset();
 auto state=cpu->capture_recall_state();
 unsigned char ready=0;std::memcpy(&ready,&state.ready,sizeof(ready));
 std::cout<<"B02 reset CPU saved_sp="<<state.ssp<<" saved_psr="<<state.sps
          <<" ready_byte="<<unsigned(ready)<<"\n";
 const bool valid=state.ssp==0&&state.sps==0&&ready==1;
 cpu->~ns32016_device();::operator delete(storage);
 device_memory_interface::s_host_space=previous;
 if(!valid){std::cerr<<"FAIL B02: reset snapshot contains uninitialized CPU fields\n";return 1;}
 std::cout<<"PASS B02 reset CPU snapshot before first instruction\n";return 0;
}
