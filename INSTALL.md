# Windows installation — both VST3 variants

This Windows x64 adaptation provides two independent VST3 instruments:

- `125A Vintage Sampler III.vst3` — alternative 125A front-panel design
- `E-MU Emulator III.vst3` — original-look panel

## Important: original EIII media are not included

**Neither the source repository nor the ROM-free VST3 download contains original E-mu boot ROMs, operating-system disk images, CDs or commercial sample banks.** You need lawfully obtained compatible original EIII files. The original project's [INSTALL.md](https://github.com/parguinsys/Emulated-3/blob/main/INSTALL.md) explains the emulated machine's media requirements; check the documentation for this Windows port for the local installation layout.

## Installing the ROM-free Windows package

1. Close Studio One or your VST3 host and extract the whole download ZIP to a normal directory.
2. Keep your legitimately obtained original EIII ROM and OS files available locally.
3. Review the included local installer scripts `INSTALLIEREN-BEIDE.cmd` and `Install-Both-Complete.ps1` before running them.
4. If you use the installer, follow its prompts to provide or select your existing local ROM/OS files. It may request administrator permission to write into the system's `Common Files\VST3` folder.
5. Restart the DAW and rescan VST3 instruments. The two variants have separate VST3 identities and can coexist.

The local installer intentionally refuses incomplete installation if required media cannot be found. It does not download, upload, or redistribute original E-mu firmware.

Alternatively, VST3 bundles can be installed manually into your normal Windows VST3 directory, but they will not boot without the correct external ROM and OS resources in the expected structure.

## Builds and source

See [BUILDING.md](BUILDING.md) for the Windows development toolchain and build commands, and [CREDITS.md](CREDITS.md) for attribution. The exact approved D0 source corresponds to the private development commit `0a90ed7c1bbf4a40fe95664aacd2d89b0df28886`. This repository's `source/` tree was independently hash-compared against the sanitized snapshot.

## Compatibility statement

This port targets **Windows x64 VST3**, and the D0 plugins were accepted by the project owner in Studio One on 29 October 2026. Compatibility with every host, firmware/media combination, or feature is not guaranteed.

This repository is **private pending public release review**.
