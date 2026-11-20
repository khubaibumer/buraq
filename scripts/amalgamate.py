#!/usr/bin/env python3
"""Generate buraq's single-header distribution under single_include/.

Outputs:
  single_include/buraq/acp.hpp             core protocol + AgentBase/ClientBase + stdio transport
                                           (zero third-party dependencies)
  single_include/buraq/runtime/connection.hpp  threaded runtime: Connection, serve_stdio, spawn_agent
  single_include/buraq/codecs/glaze.hpp    GlazeCodec adapter (requires Glaze)
  single_include/buraq/codecs/nlohmann.hpp NlohmannCodec adapter (requires nlohmann/json)

Usage:
  python3 scripts/amalgamate.py           # (re)generate files
  python3 scripts/amalgamate.py --check   # exit 1 if committed files are stale
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
INCLUDE_DIR = ROOT / "include"
OUT_DIR = ROOT / "single_include"

INCLUDE_RE = re.compile(r'^\s*#\s*include\s*[<"](buraq/[^>"]+)[>"]')
PRAGMA_ONCE_RE = re.compile(r"^\s*#\s*pragma\s+once\b")

CORE_ENTRYPOINTS = ["buraq/acp.hpp", "buraq/transport/stdio.hpp"]
# Opt-in add-ons, each emitted as its own header on top of acp.hpp (same include path as the modular tree).
ADDONS = {
    "buraq/codecs/glaze.hpp": "Requires buraq/acp.hpp from this single_include tree and Glaze.",
    "buraq/codecs/nlohmann.hpp": "Requires buraq/acp.hpp from this single_include tree and nlohmann/json.",
    "buraq/runtime/connection.hpp": "Requires buraq/acp.hpp from this single_include tree; link with threads.",
}


def project_version() -> str:
    cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    match = re.search(r"project\(\s*buraq\s+VERSION\s+([0-9][0-9.]*)", cmake)
    return match.group(1) if match else "unknown"


def banner(name: str, note: str) -> list[str]:
    return [
        "// " + "=" * 76,
        f"// buraq v{project_version()} - Agent Client Protocol (ACP) v1 SDK for C++20/23",
        "// https://github.com/khubaibumer/buraq",
        "//",
        f"// Single-header distribution: {name}",
        f"// {note}",
        "//",
        "// GENERATED FILE - DO NOT EDIT. Regenerate with: python3 scripts/amalgamate.py",
        "// SPDX-License-Identifier: Apache-2.0",
        "// " + "=" * 76,
        "#pragma once",
        "",
    ]


def read_lines(rel: str) -> list[str]:
    path = INCLUDE_DIR / rel
    if not path.is_file():
        sys.exit(f"amalgamate: missing header {path}")
    return path.read_text(encoding="utf-8").splitlines()


def expand(rel: str, seen: set[str], out: list[str]) -> None:
    if rel in seen:
        return
    seen.add(rel)
    body: list[str] = []
    for line in read_lines(rel):
        if PRAGMA_ONCE_RE.match(line):
            continue
        inc = INCLUDE_RE.match(line)
        if inc:
            expand(inc.group(1), seen, out)
            continue
        body.append(line.rstrip())
    while body and not body[0]:
        body.pop(0)
    while body and not body[-1]:
        body.pop()
    out.append(f"// ---- begin {rel} ----")
    out.extend(body)
    out.append(f"// ---- end {rel} ----")
    out.append("")


def render_core() -> str:
    out = banner("buraq/acp.hpp", "Zero third-party dependencies; codecs ship as separate headers.")
    seen: set[str] = set()
    for entry in CORE_ENTRYPOINTS:
        expand(entry, seen, out)
    return "\n".join(out).rstrip() + "\n"


def render_addon(rel: str, note: str) -> str:
    out = banner(rel, note)
    out.append('#include "' + "../" * (rel.count("/") - 1) + 'acp.hpp"')
    out.append("")
    # Core headers are already provided by acp.hpp; only inline the add-on body.
    seen = {
        "buraq/acp.hpp",
        "buraq/detail/compat.hpp",
        "buraq/protocol/jsonrpc.hpp",
        "buraq/protocol/schema_v1.hpp",
        "buraq/core/async.hpp",
        "buraq/core/agent_base.hpp",
        "buraq/core/client_base.hpp",
        "buraq/transport/stdio.hpp",
    }
    expand(rel, seen, out)
    return "\n".join(out).rstrip() + "\n"


def outputs() -> dict[Path, str]:
    result = {OUT_DIR / "buraq/acp.hpp": render_core()}
    for rel, note in ADDONS.items():
        result[OUT_DIR / rel] = render_addon(rel, note)
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--check", action="store_true", help="verify single_include/ is up to date")
    args = parser.parse_args()

    stale = []
    for path, content in outputs().items():
        rel = path.relative_to(ROOT)
        if args.check:
            current = path.read_bytes().decode("utf-8").replace("\r\n", "\n") if path.is_file() else None
            if current != content:
                stale.append(str(rel))
            continue
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(content.encode("utf-8"))
        print(f"amalgamate: wrote {rel}")

    if stale:
        print("amalgamate: stale single-header output (run `python3 scripts/amalgamate.py`):", file=sys.stderr)
        for rel in stale:
            print(f"  {rel}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
