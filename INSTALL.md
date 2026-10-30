# Installing 125A Emulated-3 — Windows x64 VST3

This guide is for the **Windows VST3 release**, not the original Linux CLAP/standalone version.

The release contains two independent instruments:

- `E-MU Emulator III.vst3` — original-style panel
- `125A Vintage Sampler III.vst3` — custom 125A panel

**No original E-mu ROMs, operating system image, CD-ROMs or commercial sample libraries are included.** Supply only lawfully obtained files from your own original media.

## 1. Exactly which original files do I need?

This release's Windows installer expects the following **exact filenames**:

| File | Required name | Size checked by installer |
|---|---|---|
| Boot ROM IC3 (low) | `e3-lsboot_ip381a_emu_systems_4088.ic3` | Must exist and be nonempty |
| Boot ROM IC4 (high) | `e3-msboot_ip380a_emu_systems_4088.ic4` | Must exist and be nonempty |
| EIII OS 2.42 hard-disk image | `EIIIOS2_42_HDImage_953MB.ISO` | **998,328,320 bytes** |

The original Emulated-3 supports additional ROM naming conventions and other raw OS disk images. **This Windows release's bundled installer is stricter:** it checks these two ROM names and that exact OS file name and file size. Renaming an incompatible image does **not** make it compatible.

For the **original boot ROM contents**, the upstream project documents the following SHA-1 checksums:

- IC3: `902c2a9a2b37b34331fb57d45b88ffabc1f12a53`
- IC4: `28b7e8991e72cc111ee0067c58bddafca70f3824`

You can check the ROMs in PowerShell with `Get-FileHash -Algorithm SHA1 <path>`. The installer checks presence and copying integrity; it does **not** verify the ROMs against those reference SHA-1 hashes.

## 2. Where should the ROMs and hard-disk image go?

Each installed VST3 bundle needs the original files in its **own** `Contents\Resources` directory.

Example using the Windows system-wide VST3 directory:

```text
C:\Program Files\Common Files\VST3\
├── E-MU Emulator III.vst3\
│   └── Contents\
│       ├── x86_64-win\
│       │   └── E-MU Emulator III.vst3
│       └── Resources\
│           ├── ROM\
│           │   ├── e3-lsboot_ip381a_emu_systems_4088.ic3
│           │   └── e3-msboot_ip380a_emu_systems_4088.ic4
│           └── OS\
│               └── EIIIOS2_42_HDImage_953MB.ISO
└── 125A Vintage Sampler III.vst3\
    └── Contents\
        ├── x86_64-win\
        │   └── 125A Vintage Sampler III.vst3
        └── Resources\
            ├── ROM\
            │   ├── e3-lsboot_ip381a_emu_systems_4088.ic3
            │   └── e3-msboot_ip380a_emu_systems_4088.ic4
            └── OS\
                └── EIIIOS2_42_HDImage_953MB.ISO
```

The installer may instead keep the plugins in an existing VST3 subfolder (for example `C:\Program Files\Common Files\VST3\Eigene`). The important part is the structure **inside each .vst3 bundle**, not the optional `Eigene` folder.

## 3. Simplest installation method: bundled installer

1. Close Studio One or your other VST3 host.
2. Extract the entire release ZIP to a folder.
3. Keep the original media together in a folder such as:

   ```text
   C:\EIII-Media\
   ├── ROM\
   │   ├── e3-lsboot_ip381a_emu_systems_4088.ic3
   │   └── e3-msboot_ip380a_emu_systems_4088.ic4
   └── OS\
       └── EIIIOS2_42_HDImage_953MB.ISO
   ```

4. Review `INSTALLIEREN-BEIDE.cmd` and `Install-Both-Complete.ps1`, then run `INSTALLIEREN-BEIDE.cmd`. Administrator authorization may be needed to write to `C:\Program Files\Common Files\VST3`.
5. The installer first searches existing VST3 plugins for your original media. If it finds none, it opens a folder chooser: select the **top-level** media folder (`C:\EIII-Media` in this example), **not** the ROM or OS subfolder.
6. The installer copies and verifies both plugins, the two ROMs and the OS image. It will stop without installing incomplete plugins if the required media are not present or the OS file size is wrong. It does not download any original E-mu material.
7. Restart the DAW and rescan VST3 instruments.

**Manual alternative:** Place the three files into the two VST3 bundles exactly as shown in section 2, then install the completed bundles in your normal VST3 directory. Do not install multiple copies of the same plugin identity in different locations.

## 4. Hard disk and sound-library discs in the emulator

The required OS image above is the **boot hard disk**, mounted as **D1** by the plugin. It is *not* a CD-ROM or a sample-bank ZIP.

Optional legally obtained EIII CD-ROM images can be selected through the plugin's drive/media controls (typically **D3**; additional supported devices may appear under D4–D7). CD images are **not** copied into the ROM or OS directories just to install the plugin.

To load a factory bank, use the original EIII's Load Bank controls. Many factory banks expect **MIDI channel 14**, so check the MIDI channel if you hear nothing.

## 5. Troubleshooting

- **Installer says ROM/OS missing:** check the names, exact folder structure and the OS image size; when prompted, select the containing media folder, not `ROM` or `OS`.
- **Plugin window opens but the EIII does not boot:** verify that both ROM dumps match the original checksums and that your disk image actually contains a compatible EIII OS.
- **No audio:** load a bank or preset and check the bank's MIDI channel (often channel 14).
- **Two separate installations appear in the DAW:** check for duplicate plugin bundles in different VST3 folders.
- **No CD library:** the OS boot disk D1 is not a factory sound library; provide your own EIII-format CD/image through the relevant media menu.

This is an independent, unofficial Windows VST3 port of [parguinsys/Emulated-3](https://github.com/parguinsys/Emulated-3). See [CREDITS.md](CREDITS.md), [LICENSE](LICENSE) and [BUILDING.md](BUILDING.md) for attribution, licensing and source.
