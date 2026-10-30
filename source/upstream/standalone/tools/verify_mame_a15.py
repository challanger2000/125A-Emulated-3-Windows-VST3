#!/usr/bin/env python3
"""A15 byte-exact original vendoring and current-state verification."""
import hashlib
import json
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1] / "third_party" / "mame"
BASE = "42cdc330"
EXPECTED_CHANGED = {
    # B02 adds value-only CD generation and complete serial snapshot APIs.
    # Historical MANIFEST.sha256 stays unchanged; see the B02 repair report.
    "src/devices/bus/nscsi/cd.h",
    "src/emu/diserial.h",
    # Already present at the user-pinned B02 baseline 6bd34a7: target/bus
    # recall APIs and deterministic FPU operand reset, not new timing edits.
    "src/devices/bus/nscsi/hd.cpp",
    "src/devices/bus/nscsi/hd.h",
    "src/devices/machine/ncr5380.h",
    "src/devices/machine/ns32081.cpp",
    "src/devices/machine/nscsi_bus.cpp",
    "src/devices/machine/nscsi_bus.h",
    "src/devices/bus/nscsi/cd.cpp",
    "src/devices/cpu/ns32000/ns32000.h",
    "src/devices/machine/ns32081.h",
    "src/devices/machine/nscsi_hle.cpp",
    "src/devices/machine/nscsi_hle.h",
    "src/devices/machine/z80scc.h",
}
def digest(data):
    return hashlib.sha256(data).hexdigest()

def git_bytes(revision, path):
    return subprocess.check_output(["git", "show", revision + ":" + path])

def main():
    historical = {}
    for line in (ROOT / "MANIFEST.sha256").read_text().splitlines():
        h, p = line.split("  ./", 1)
        historical[p] = h
    original_bad = []
    now_bad = []
    current = {}
    for name, original_sha in sorted(historical.items()):
        repo_path = "upstream/standalone/third_party/mame/" + name
        original = digest(git_bytes(BASE, repo_path))
        if original != original_sha:
            original_bad.append({"path": name, "manifest": original_sha, "original_import": original})
        current_sha = digest((ROOT / name).read_bytes())
        current[name] = current_sha
        if current_sha != original_sha:
            now_bad.append(name)
    actual_changed = set(now_bad)
    errors = {
        "unexpected_changed": sorted(actual_changed - EXPECTED_CHANGED),
        "expected_change_missing": sorted(EXPECTED_CHANGED - actual_changed),
        "original_import_mismatches": original_bad,
    }
    expected_files = set(historical) | {"PROVENANCE.md"}
    present_files = {p.relative_to(ROOT).as_posix() for p in ROOT.rglob("*") if p.is_file() and p.name != "MANIFEST.sha256"}
    errors["missing"] = sorted(expected_files - present_files)
    errors["unlisted"] = sorted(present_files - expected_files)
    snapshot = "\n".join(f"{h}  ./{name}" for name, h in sorted(current.items())) + "\n"
    out = Path("a15-current-sha256.txt")
    out.write_text(snapshot)
    report = {"original_commit": BASE, "original_checked": len(historical),
              "current_changed": now_bad, "current_snapshot_sha256": digest(snapshot.encode()),
              "errors": errors, "pass": not any(errors.values())}
    Path("a15-verification.json").write_text(json.dumps(report, indent=2)+"\n")
    print(json.dumps(report, indent=2))
    return 0 if report["pass"] else 1

if __name__ == "__main__":
    raise SystemExit(main())
