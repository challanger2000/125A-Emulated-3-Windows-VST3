# 125A Emulated-3 — Windows VST3

**Windows x64 VST3 port of [Emulated-3](https://github.com/parguinsys/Emulated-3), the unofficial E-mu Emulator III sampler emulator.**

> **Windows x64 VST3 v1.0.0 is publicly available.** [Download the Windows release](https://github.com/challanger2000/125A-Emulated-3-Windows-VST3/releases/tag/v1.0.0). Original E-mu ROM/OS media are not included.\n
## Two Windows VST3 instruments

- **125A Vintage Sampler III** — custom 125A front-panel design.
- **E-MU Emulator III** — front-panel design closer to the original rack hardware.

Both instruments share the Emulated-3 engine and have distinct VST3 identities.

## Credits and acknowledgements

**Special thanks to [parguinsys](https://github.com/parguinsys/Emulated-3)** for developing Emulated-3 and making this Windows adaptation possible. The core emulation is derived from that work, not a separate 125A emulation written from scratch.

Thanks also to the contributors to MAME, Berkeley SoftFloat, CLAP, stb, the open-source typefaces, and the Steinberg VST3 development tooling. Detailed provenance and applicable third-party license notices will accompany the source and release.

## Installation — required ROMs and OS image

**[Windows installation guide: exact ROM filenames, OS image name, sizes and directories](INSTALL.md)**

The Windows installer requires the exact filenames listed below and the original files you provide yourself:

- `e3-lsboot_ip381a_emu_systems_4088.ic3` — ROM IC3
- `e3-msboot_ip380a_emu_systems_4088.ic4` — ROM IC4
- `EIIIOS2_42_HDImage_953MB.ISO` — OS hard disk (998,328,320 bytes)

Both VST3 bundles use their own `Contents/Resources/ROM` and `Contents/Resources/OS` directories. The included installer copies the original media from your local media folder into both bundles.

## Firmware and samples

**No proprietary E-mu boot ROMs, operating-system images or commercial sound banks are included.** Users need their own lawfully obtained original EIII ROM and OS media.

## Platform and status

Windows x64 VST3. The owner approved the D0 binaries on 29 October 2026 and confirmed operation in Studio One. The matching source reference is `0a90ed7c1bbf4a40fe95664aacd2d89b0df28886` in the private development repository. The first public Windows x64 VST3 release is available as v1.0.0.

## Licensing

The Emulated-3-derived code is governed by the **GNU General Public License v3**. Original copyright statements and third-party license requirements must be preserved. The relevant source files and build instructions are included in this repository, alongside the applicable license notices.

This project is independent and is **not affiliated with, sponsored by or endorsed by E-mu Systems or Creative Technology**. Product and company names remain the property of their respective owners.
