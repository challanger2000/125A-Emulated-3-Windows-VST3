# Build instructions — Windows x64 / VST3

This source is the Windows VST3 adaptation of [parguinsys/Emulated-3](https://github.com/parguinsys/Emulated-3) under GPL-3.0. The approved D0 reference source commit is `0a90ed7c1bbf4a40fe95664aacd2d89b0df28886`.

## Requirements

- Windows 10/11 x64
- Visual Studio 2022 with C++ desktop development workload
- CMake with Visual Studio 17 2022 generator
- Steinberg VST3 SDK with all submodules: https://github.com/steinbergmedia/vst3sdk
- All corresponding sources and third-party dependencies preserved under `source/`

The original E-mu firmware and OS are **not** required to compile, and **not** distributed. For actual use, the user must supply authorized original ROMs and an EIII-compatible OS disk image.

## Build both instrument variants

From the root of this source tree in PowerShell:

```powershell
git clone --depth 1 --recurse-submodules --shallow-submodules https://github.com/steinbergmedia/vst3sdk.git vst3sdk
cmake -S source/windows/vst3-port -B build/vst3 -G "Visual Studio 17 2022" -A x64 -DVST3_SDK_ROOT="$PWD/vst3sdk"
cmake --build build/vst3 --config Release --target EIII125A VintageSamplerIII125A --parallel 4
```

The build targets are `EIII125A` and `VintageSamplerIII125A`. To install the two VST3 bundles, rename the compiled modules as directed in the packaging source and verify module DLLs have the final bundle name; do not install duplicate variants with the same VST3 IDs.

## Test evidence and release status

The owner-approved D0 source revision built successfully in GitHub Actions on 29 October 2026 (run #40793580607), including the Steinberg validator, core regression tests and ROM-free host lifecycle checks. The owner subsequently confirmed operation in Studio One. This is not a promise of broad host compatibility, nor a certification of every EIII operation.

Public distribution requires a separate legal/assets review and matching package validation. See the original upstream GPL and third-party notices distributed here.
