#include "../../upstream/runtime/eiii_engine.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>
int main() {
    using namespace std::chrono_literals;
    eiii::engine first,second;eiii::engine_config cfg;
    first.start(cfg);second.start(cfg);
    assert(!first.import_wav("missing.wav",{60,80,20,0}));
    for(unsigned trial=0;trial<8;++trial) {
        assert(first.import_wav("125A-DOES-NOT-EXIST.wav",{60,48,72,0}));
        // Cancellation is idempotent and belongs to this instance only.
        first.cancel_wav_import();first.cancel_wav_import();
        assert(!second.wav_import_busy());
        eiii::media_result result;
        const auto timeout=std::chrono::steady_clock::now()+3s;
        while(!first.poll_media_result(result)) {
            assert(std::chrono::steady_clock::now()<timeout);std::this_thread::sleep_for(1ms);
        }
        assert(result.slot==eiii::media_slot::wav_import&&!result.ok&&!result.error.empty());
        while(first.wav_import_busy()){assert(std::chrono::steady_clock::now()<timeout);std::this_thread::yield();}
        assert(first.hd_changes(1).empty() && second.hd_changes(1).empty());
    }
    assert(first.import_wav("missing.wav",{}));first.shutdown();assert(!first.wav_import_busy());second.shutdown();
    std::cout<<"PASS real engine worker: missing media, invalid options, cancellation, repeated jobs, shutdown and instance isolation\n";
}
