#include "../../upstream/runtime/latency_policy.h"
#include <cassert>
#include <cstdio>
int main() {
 assert(eiii::latency_for(48000,1024,32768)==2560);
 assert(eiii::latency_for(44100,64,32768)==941);
 assert(eiii::latency_for(96000,1024,32768)==2560);
 assert(eiii::latency_for(48000,32768,32768)==16384);
 assert(eiii::latency_for(0,1024,32768)==0);
 std::puts("PASS host/engine shared latency policy");
}
