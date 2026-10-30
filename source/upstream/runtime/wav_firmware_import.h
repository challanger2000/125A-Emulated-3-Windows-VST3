#pragma once
#include "wav_import.h"
#include "eiii_runtime.h"
#include <atomic>
#include <memory>
namespace eiii::wav {
struct Result {
    std::unique_ptr<runtime> machine;
    runtime_config config;
    int bank=-1, backup=-1;
};
// Runs solely on the engine owner thread. All guest writes are performed by
// original firmware via front-panel controls. Existing source images remain rb.
// On failure the original running machine is retained; a completed safety bank
// may have been added to its private disk overlay, never overwriting a bank.
bool execute(runtime& original, const runtime_config& config, const Audio& audio,
             const Options& options, const std::atomic<bool>& cancel,
             Result& result, std::string& error);
}
