"""Cut a GitHub release for FlipperHA with a clean-built FAP.

The released FAP is built from a `git archive` of HEAD, NOT the working tree, so
it can never pick up a local `flipper_app/src/ha_remote_local.h` (an optional
compile-time override that bakes in a private bridge URL/key). That keeps the
public download on placeholder defaults.

Standard release step (run after bumping `fap_version` in application.fam,
committing, and pushing to main):

    py tools/release.py                        # notes auto-generated from commits
    py tools/release.py --notes-file NOTES.md  # hand-written release notes
    py tools/release.py --draft                # stage a draft to review first

Requires: git, gh (authenticated), and ufbt (`py -m ufbt`).
"""
from __future__ import annotations

import argparse
import io
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
from pathlib import Path

REPO = "CannonWest/FlipperHA"
ROOT = Path(__file__).resolve().parents[1]
FAM = ROOT / "flipper_app" / "application.fam"
UFBT_FAP = Path.home() / ".ufbt" / "build" / "flipperha.fap"


def sh(cmd, **kw):
    print("+", " ".join(map(str, cmd)), flush=True)
    return subprocess.run(cmd, check=True, **kw)


def capture(cmd) -> str:
    return subprocess.run(cmd, check=True, capture_output=True, text=True).stdout.strip()


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--notes-file", help="Markdown release notes; omit to auto-generate.")
    ap.add_argument("--draft", action="store_true")
    args = ap.parse_args()

    m = re.search(r'fap_version="([^"]+)"', FAM.read_text(encoding="utf-8"))
    if not m:
        sys.exit("could not read fap_version from application.fam")
    tag = f"v{m.group(1)}"

    if capture(["git", "-C", str(ROOT), "status", "--porcelain"]):
        print("WARNING: working tree is dirty; releasing committed HEAD, not local edits.")
    head = capture(["git", "-C", str(ROOT), "rev-parse", "--short", "HEAD"])
    print(f"Releasing {tag} @ {head}")

    # Preflight: this interpreter must have ufbt (run with `py` / `py -3.14`, not a
    # Store-aliased python — that shadows ufbt and breaks the build subprocess).
    if subprocess.run([sys.executable, "-m", "ufbt", "--version"], capture_output=True).returncode:
        sys.exit(f"ufbt not available under {sys.executable}\n"
                 f"  Run with a Python that has ufbt, e.g.:  py -3.14 tools/release.py")

    tmp = Path(tempfile.mkdtemp(prefix="flipperha-rel-"))
    try:
        # 1. clean snapshot of HEAD (gitignored ha_remote_local.h / .env are NOT in it)
        archive = subprocess.run(
            ["git", "-C", str(ROOT), "archive", "HEAD"], check=True, capture_output=True
        ).stdout
        with tarfile.open(fileobj=io.BytesIO(archive)) as tf:
            tf.extractall(tmp, filter="data")
        if (tmp / "flipper_app" / "src" / "ha_remote_local.h").exists():
            sys.exit("ABORT: ha_remote_local.h is in the archive — would bake in private config.")

        # 2. clean build (uses the global ufbt SDK)
        sh([sys.executable, "-m", "ufbt", "build"], cwd=tmp / "flipper_app")
        if not UFBT_FAP.exists():
            sys.exit("build produced no fap")

        # 3. safety gate: the placeholder default must be present, proving no private
        #    override compiled in.
        if b"your-bridge.example.com" not in UFBT_FAP.read_bytes():
            sys.exit("ABORT: placeholder bridge URL missing — fap may carry a private override.")
        staged = tmp / "flipperha.fap"
        shutil.copy(UFBT_FAP, staged)
        print(f"clean fap: {staged.stat().st_size} bytes")

        # 4. release
        cmd = ["gh", "release", "create", tag, "--repo", REPO, "--target", "main",
               "--title", f"FlipperHA {tag}"]
        cmd += ["--notes-file", args.notes_file] if args.notes_file else ["--generate-notes"]
        if args.draft:
            cmd.append("--draft")
        cmd.append(f"{staged}#flipperha.fap (Flipper app - Target 7 / API 87.1)")
        sh(cmd)
        print(f"Released {tag}: https://github.com/{REPO}/releases/tag/{tag}")
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
