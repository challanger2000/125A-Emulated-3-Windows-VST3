# Vendored MAME sources

Copied by `tools/vendor_mame.sh` from `/home/paul/EIII_RE/mame` at commit `eb3856b8de1fd1209bbfa1968916c658e0273f14`, **including the working-tree modifications**
listed in `LOCAL_CHANGES.diff` (the proven EIII SCSI behaviour: `ncr5380.cpp`, `nscsi_hle.cpp/.h`, `bus/nscsi/cd.cpp`).
Files are unmodified copies of that working tree; `MANIFEST.sha256` pins every byte.

Licences: every MAME file here is `license:BSD-3-Clause` (src/lib/util/disasmintf.cpp carries no tag; MAME's src/lib is
BSD-3-Clause). Berkeley SoftFloat 3e: see `3rdparty/softfloat3/COPYING.txt` (BSD-style, John R. Hauser).
No E-mu ROM, operating system, sample or bank data is included anywhere in this tree.

## 125A A15 integrity audit (2026-10-21)

This is historical upstream provenance. Keep MANIFEST.sha256 unchanged as evidence of the original vendoring; later 125A modifications require a separate current-state SHA-256 inventory and documented diffs. Never silently refresh the historical digest list. Full 498-entry comparison is still pending, so this note is not an A15 PASS.

## B02 continuation repair inventory (2026-10-24)

The historical `MANIFEST.sha256` and original `LOCAL_CHANGES.diff` are unchanged.
The separate current 498-file inventory is committed in
`docs/evidence/b02-20261024/a15-current-sha256.txt`, with verification report and
baseline attribution beside it. All original-import bytes at
`42cdc330ea8bd40473fde643f74eb2196df61858` match the historical 498 hashes.

There are now 14 deliberately changed historical paths, with zero unexpected
changes, missing paths or unexpected unlisted files. The six paths omitted by
the older verifier (`hd.cpp/.h`, `ncr5380.h`, `ns32081.cpp`, `nscsi_bus.cpp/.h`)
were already present byte-identically at the user-pinned baseline `6bd34a7`;
`a15-baseline-attribution.json` records their hashes and source commits. The
verifier now explicitly accounts for those existing B02 APIs.

This repair adds complete value-only serial recall in `src/emu/diserial.h`,
CD-generation recall fields/API in `cd.cpp/.h`, snapshot-safe initial scratch
values in `ns32000.h` and `z80scc.h`, and SCC latch accessors. It does not patch
ROMs, CPU instructions or hardware event dispatch. See the main B02 repair
report for exact diffs, runtime tests and remaining Windows/DAW gates.

Current inventory SHA256: `dd4f320fda4c054305e37780c98692e9aae4e178095ed6e9e387390ad9e273f4`.
