# D0 — release preparation record (30 October 2026)

This repository is **PRIVATE** pending the repository owner's authorization to publish.

## Verified source
- Independent new GitHub repository with no private development commit ancestry.
- Full corresponding Windows x64 VST3 source code is present under `source/`.
- All 878 files under `source/` exactly reproduce the sanitized owner-approved D0 source snapshot, **including executable file modes**.
- Source directory Git tree SHA-1: `09f979265783e443a7e71d425f2af420297a5eb8`.
- Owner-approved original D0 source commit: `0a90ed7c1bbf4a40fe95664aacd2d89b0df28886`.
- Original Emulated-3 baseline: `parguinsys/Emulated-3@8dd3407941fd241df2ec504ae3051bc65e08f2f6`.
- `PORT_SOURCE_MANIFEST.json` lists 234 modified/new D0 source files with expected Git blob SHAs.
- Unchanged original upstream source was vendored with GitHub Actions, successful run [#41132745597](https://github.com/challanger2000/125A-Emulated-3-Windows-VST3/actions/runs/41132745597).
- Root `LICENSE` contains GPL version 3; origin and third-party license notices remain in `source/`.
- File-path audit of the exact source snapshot showed **no separate E-mu ROM, OS, disk image, bank, VST3 binary, EXE or DLL assets**. This is not a guarantee against all unrecognized embedded/private content.

## Windows binary candidate
Two unchanged plugins are drawn from the successfully completed Windows Actions build [#40793580607](https://github.com/challanger2000/125A-Vintage-Sampler-III-Final/actions/runs/40793580607), produced from the approved D0 source revision.

- `125A Vintage Sampler III.vst3` module SHA-256: `81ce2e0825cc47197e7fe75cd3ce608959a44e430ce874bf607a572e68fc9865`
- `E-MU Emulator III.vst3` module SHA-256: `68d4f4e9152327a5b0629d26918f098b28739720c6e6a3cef7c32b190c97b260`
- Private release-candidate archive: `125A-Emulated-3-Windows-VST3-D0-RELEASE-CANDIDATE.zip`
- Candidate ZIP SHA-256: `4b95e838bc37d724c86556216c78a20708fa4c8f0eed18b3ffeda190d2a28616`
- Candidate ZIP includes both VST3s, local-only installer scripts, GPL, attribution, actual third-party notices, installation directions, source link and SHA-256 manifests.
- No separate proprietary E-mu ROM, OS disk image or sample media appears in the candidate archive's manifest.
- D0 host CI succeeded and the owner confirmed operation in Studio One. This does not certify every host or original EIII-media feature.

## Remaining steps before publication

1. Final human review of source and package rights, artwork, comments, any residual private information, and GitHub repository details.
2. Make this source repository public **before** distributing the GPL-covered binaries.
3. Add the identical verified ROM-free candidate ZIP as a GitHub Release asset. This GitHub connector currently has no action to upload Release assets or change repository visibility, so these specific GitHub UI operations must be completed by the owner.
4. Keep privately owned original E-mu firmware and sound media out of the repository/releases.
5. Announce the release respectfully to the original Emulated-3 author, `parguinsys`, with links to the GPL-covered source and public download.

The original private project and owner-approved complete VST3 packages were not modified. No public release has been made.
