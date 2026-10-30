// Shared D0 snapshot wire signature, independent of any MAME headers.
#pragma once
#include <cstdint>
namespace eiii::vst3 {
struct CoreD0Format {
 static constexpr uint32_t tag=0x31523044u; // D0R1
 static constexpr uint32_t version=1;
};
}
