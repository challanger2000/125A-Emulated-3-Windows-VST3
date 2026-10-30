#!/usr/bin/env python3
"""Vendor ONLY the missing files from the pinned public Emulated-3 upstream.

The 125A Windows port and its altered upstream files are already tracked in
this repository. Never overwrite them; check their Git blob SHAs first.
This tool fetches exclusively the public parguinsys/Emulated-3 Git repository.
"""
from __future__ import annotations

import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent
DEST = ROOT / "source" / "upstream"
SOURCE_URL = "https://github.com/parguinsys/Emulated-3.git"
REVISION = "8dd3407941fd241df2ec504ae3051bc65e08f2f6"
EXPECTED_UPSTREAM_FILES = 684
EXPECTED_FINAL_UPSTREAM_FILES = 713


def run(*args: str, cwd: Path | None = None) -> str:
    return subprocess.check_output(args, cwd=cwd, text=True).strip()


def git_blob_sha(path: Path) -> str:
    return run("git", "hash-object", str(path))


def main() -> None:
    manifest = json.loads((ROOT / "PORT_SOURCE_MANIFEST.json").read_text("utf-8"))
    if manifest["original_upstream"] != "parguinsys/Emulated-3@" + REVISION:
        raise RuntimeError("Pinned upstream revision mismatch")

    # Fail if any Windows port/modified emulation source is absent or changed.
    for item in manifest["files"]:
        dest = ROOT / item["path"]
        if not dest.is_file() or dest.is_symlink():
            raise RuntimeError("Missing or unsafe overlay source: " + item["path"])
        if git_blob_sha(dest) != item["sha"]:
            raise RuntimeError("Modified pinned overlay source: " + item["path"])

    with tempfile.TemporaryDirectory(prefix="emulated3-source-") as tmp:
        origin = Path(tmp) / "emulated3"
        origin.mkdir()
        run("git", "init", "-q", str(origin))
        run("git", "remote", "add", "origin", SOURCE_URL, cwd=origin)
        run("git", "fetch", "--depth=1", "origin", REVISION, cwd=origin)
        run("git", "checkout", "--detach", "-q", "FETCH_HEAD", cwd=origin)
        if run("git", "rev-parse", "HEAD", cwd=origin) != REVISION:
            raise RuntimeError("Unexpected upstream Git revision")

        originals = [s for s in run("git", "ls-files", "-z", cwd=origin).split("\0") if s]
        # git ls-files -z through text subprocess has a trailing zero separator
        if len(originals) != EXPECTED_UPSTREAM_FILES:
            raise RuntimeError(f"Upstream files mismatch: {len(originals)}")

        copied = 0
        for rel in originals:
            path = Path(rel)
            if path.is_absolute() or ".." in path.parts or rel.startswith(".git/"):
                raise RuntimeError("Unsafe source path: " + rel)
            src = origin / rel
            target = DEST / rel
            if src.is_symlink():
                raise RuntimeError("Unexpected symlink in original source: " + rel)
            if target.exists():
                if target.is_symlink():
                    raise RuntimeError("Unexpected overlay symlink: " + rel)
                continue
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(src, target)
            copied += 1

        count = len(list(DEST.rglob("*")))
        actual_files = sum(p.is_file() for p in DEST.rglob("*"))
        if actual_files != EXPECTED_FINAL_UPSTREAM_FILES:
            raise RuntimeError(f"Wrong final upstream source count: {actual_files}")
        print(f"UPSTREAM_IMPORT_PASS upstream={REVISION} copied={copied} resulting_files={actual_files}")


if __name__ == "__main__":
    main()
