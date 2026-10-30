// Compatibility include: the same allocation-free adapter serves host and WAV input.
#pragma once
#include "../../upstream/runtime/sample_input.h"
namespace eiii::vst3 {
using eiii::sampling::SampleInput;
using eiii::sampling::copy_sample_input;
}
