# Credits and acknowledgements (public release draft)

## Original emulation project

**Emulated-3** by **parguinsys**: https://github.com/parguinsys/Emulated-3

This Windows VST3 adaptation is based on that original project. The underlying E-mu Emulator III emulation, hardware modelling, emulated operating-system interfaces, and much of the virtual front panel originate from Emulated-3. We are grateful for the original developer's substantial work and for making it available as open-source software.

Upstream baseline recorded in this project:
`parguinsys/Emulated-3@8dd3407941fd241df2ec504ae3051bc65e08f2f6`.
Later modifications and Windows/VST3 integration are maintained by **125A Systems**; original copyright, attribution and GPL notices are preserved.

## Other upstream and third-party projects

This project also uses or builds upon components and tooling from the following projects. Their own license and attribution notices must be retained:

- **MAME** device emulation code (BSD-3-Clause): https://github.com/mamedev/mame
- **Berkeley SoftFloat**, by John R. Hauser (see included BSD-style license): https://www.jhauser.us/arithmetic/SoftFloat.html
- **CLAP** interface headers (MIT): https://github.com/free-audio/clap
- **stb** libraries (see included licenses): https://github.com/nothings/stb
- **Archivo Narrow** and **Noto Sans** typefaces (SIL Open Font License 1.1); their license texts accompany the existing font files
- **Steinberg VST 3 SDK** for the Windows VST3 implementation; SDK is fetched separately during build, with its own applicable licensing conditions: https://github.com/steinbergmedia/vst3sdk

See also `source/upstream/LICENSE`, `source/upstream/third_party/`, `source/upstream/standalone/third_party/mame/`, `source/UPSTREAM-PROVENANCE.txt` and the upstream project's attribution notices.

## Note on E-mu

This is an independent, unofficial software project and is not affiliated with, endorsed by, or sponsored by E-mu Systems or Creative Technology. Names referring to E-mu and Emulator III identify the hardware being emulated and belong to their respective trademark owners.

**No proprietary E-mu boot ROMs, operating system images, or commercial sound banks are distributed.** Users need legitimately obtained, compatible original media.

## Thank you

Special thanks to **parguinsys** for laying the foundation for this Windows VST3 port and to the maintainers and contributors of the open-source projects above. This project would not exist in its present form without their work.

---

Draft: verify final shipped dependency set and corresponding license files before publishing. No public release is implied by this file.
