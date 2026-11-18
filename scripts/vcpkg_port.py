#!/usr/bin/env python3
"""Maintain the vcpkg port in ports/buraq.

  scripts/vcpkg_port.py pin [--version X.Y.Z]   Set version + SHA512 of the GitHub tag archive (after tagging).
  scripts/vcpkg_port.py overlay OUT_DIR          Write an overlay port that builds the local working tree
                                                 (CI / testing unreleased changes).
"""
import argparse
import hashlib
import json
import re
import sys
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PORT = ROOT / "ports" / "buraq"
REPO = "khubaibumer/buraq"
FROM_GITHUB = re.compile(r"vcpkg_from_github\(.*?\n\)\n", re.S)


def cmake_version() -> str:
    text = (ROOT / "CMakeLists.txt").read_text()
    return re.search(r"project\(buraq VERSION ([0-9]+\.[0-9]+\.[0-9]+)", text).group(1)


def pin(version: str) -> None:
    url = f"https://github.com/{REPO}/archive/v{version}.tar.gz"
    with urllib.request.urlopen(url) as resp:
        sha = hashlib.sha512(resp.read()).hexdigest()
    portfile = PORT / "portfile.cmake"
    portfile.write_text(re.sub(r"SHA512 \S+", f"SHA512 {sha}", portfile.read_text()))
    manifest_path = PORT / "vcpkg.json"
    manifest = json.loads(manifest_path.read_text())
    manifest["version"] = version
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"vcpkg port pinned to v{version} (SHA512 {sha[:16]}...)")


def overlay(out_dir: Path) -> None:
    dest = out_dir / "buraq"
    dest.mkdir(parents=True, exist_ok=True)
    source = ROOT.as_posix()
    portfile = FROM_GITHUB.sub(f'set(SOURCE_PATH "{source}")\n', (PORT / "portfile.cmake").read_text(), count=1)
    if "vcpkg_from_github" in portfile:
        sys.exit("vcpkg_port: could not rewrite vcpkg_from_github block")
    (dest / "portfile.cmake").write_text(portfile)
    for name in ("vcpkg.json", "usage"):
        (dest / name).write_text((PORT / name).read_text())
    print(f"vcpkg overlay port written to {dest} (source: {source})")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="cmd", required=True)
    p_pin = sub.add_parser("pin")
    p_pin.add_argument("--version", default=None)
    p_overlay = sub.add_parser("overlay")
    p_overlay.add_argument("out_dir", type=Path)
    args = parser.parse_args()
    if args.cmd == "pin":
        pin(args.version or cmake_version())
    else:
        overlay(args.out_dir)


if __name__ == "__main__":
    main()
