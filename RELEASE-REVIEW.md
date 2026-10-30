# v1.0.0 — release and source provenance (30 October 2026)

This is a **historical record** of the first published Windows x64 VST3 release. The repository is **PUBLIC**.

## Source provenance

- Independent GitHub repository with no private development commit ancestry.
- Full corresponding Windows x64 VST3 source code is present under `source/`.
- The original D0 source snapshot contained 878 files under `source/`, including executable file modes.
- Source directory Git tree SHA-1 before subsequent documentation-only changes: `09f979265783e443a7e71d425f2af420297a5eb8`.
- Owner-approved original D0 source commit: `0a90ed7c1bbf4a40fe95664aacd2d89b0df28886`.
- Original Emulated-3 baseline: `parguinsys/Emulated-3@8dd3407941fd241df2ec504ae3051bc65e08f2f6`.
- `PORT_SOURCE_MANIFEST.json` identifies the Windows-port modified/new files.
- Unchanged original upstream source was vendored with GitHub Actions, successful run [#41132745597](https://github.com/challanger2000/125A-Emulator-III-Windows-VST3/actions/runs/41132745597).
- Root `LICENSE` contains GPL version 3; attribution and third-party license notices are retained.

## Released VST3 modules

The two unchanged Windows x64 VST3 modules originate from the successfully completed Windows Actions [build #40793580607](https://github.com/challanger2000/125A-Vintage-Sampler-III-Final/actions/runs/40793580607), produced from the approved D0 source.

- `125A Vintage Sampler III.vst3` module SHA-256: `81ce2e0825cc47197e7fe75cd3ce608959a44e430ce874bf607a572e68fc9865`
- `E-MU Emulator III.vst3` module SHA-256: `68d4f4e9152327a5b0629d26918f098b28739720c6e6a3cef7c32b190c97b260`
- Public release: [v1.0.0](https://github.com/challanger2000/125A-Emulator-III-Windows-VST3/releases/tag/v1.0.0).
- Public ZIP uploaded on 30 October 2026: `125A-Emulated-3-Windows-VST3-v1.0.0-ROM-FREE.zip` (the filename was chosen before the repository rename).
- Existing public ZIP SHA-256 as verified on GitHub: `8c389a566aa8ebd88c388a2440ad4ecd6ef329179b59f365bb241e29ee87e8f5`.
- The installer requires the owner's legally obtained EIII original boot ROMs and OS disk image. These are **not distributed**.
- Owner confirmed operation in Studio One; this does not certify every host or original EIII-media feature.

## Original project and acknowledgements

The Windows port is derived from [Emulated-3 by parguinsys](https://github.com/parguinsys/Emulated-3); that original project name must remain intact for attribution. The author was thanked publicly in [issue #6](https://github.com/parguinsys/Emulated-3/issues/6).

This record describes the publication status as of 30 October 2026; the published ZIP may be renamed later without changing the VST3 modules.
