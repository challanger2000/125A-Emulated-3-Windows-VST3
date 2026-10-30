#!/usr/bin/env python3
"""Audit vendored MAME files without rewriting the historical SHA-256 manifest."""
import argparse
import hashlib
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1] / "third_party" / "mame"
MANIFEST = ROOT / "MANIFEST.sha256"
LINE = re.compile(r"^([0-9a-f]{64})  \./(.+)$")

def audit():
    expected = {}
    errors = []
    for number, line in enumerate(MANIFEST.read_text(encoding="utf-8").splitlines(), 1):
        match = LINE.fullmatch(line)
        if not match:
            errors.append({"kind": "invalid_manifest_line", "line": number})
            continue
        digest, relative = match.groups()
        candidate = Path(relative)
        if candidate.is_absolute() or ".." in candidate.parts or relative in expected or relative == "MANIFEST.sha256":
            errors.append({"kind": "invalid_or_duplicate_path", "line": number, "path": relative})
            continue
        expected[relative] = digest
    current = {}
    for path in sorted(ROOT.rglob("*")):
        if path.is_symlink():
            errors.append({"kind": "symlink_not_supported", "path": path.relative_to(ROOT).as_posix()})
            continue
        if not path.is_file() or path == MANIFEST:
            continue
        name = path.relative_to(ROOT).as_posix()
        current[name] = hashlib.sha256(path.read_bytes()).hexdigest()
    mismatch = [{"path": name, "historical_sha256": expected[name], "current_sha256": current[name]}
                for name in sorted(expected.keys() & current.keys()) if expected[name] != current[name]]
    missing = sorted(expected.keys() - current.keys())
    extra = sorted(current.keys() - expected.keys())
    result = {
        "manifest": MANIFEST.relative_to(ROOT).as_posix(),
        "historical_entries": len(expected), "current_files": len(current),
        "matching_files": len(expected.keys() & current.keys()) - len(mismatch),
        "mismatches": mismatch, "missing": missing, "unlisted": extra,
        "errors": errors, "historical_manifest_untouched": True,
    }
    return result

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--json", type=Path, help="write machine-readable audit without modifying the manifest")
    args = parser.parse_args()
    result = audit()
    output = json.dumps(result, indent=2, sort_keys=True) + "\n"
    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(output, encoding="utf-8")
    print(output)
    return 1 if result["mismatches"] or result["missing"] or result["unlisted"] or result["errors"] else 0

if __name__ == "__main__":
    raise SystemExit(main())
