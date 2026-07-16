#!/usr/bin/env python3
"""Public surface contract gate for Core 10.0.0.

Verifies that the installed public header closure and the built shared
library expose exactly the C surface defined by the formal spec under
core/doc/spec/core/, and that every identifier removed by the 10.0.0
contract is absent.

Checks:
  1. Korean and English formal spec files carry identical C blocks.
  2. Header closure reachable from core/include/zlink.h declares every
     formal function, and no function outside the formal set.
  3. Formal types, enum types, enumerators, struct fields and macros are
     all present in the header closure.
  4. Identifiers listed in removed-identifiers-10.0.0.json are absent.
  5. Dynamic exports of libzlink match the formal function set exactly
     (no headerless internal exports).

Usage: check_public_surface.py <repo_root> <libzlink_path>
Exit code 0 on success, 1 on contract violation.
"""

import json
import pathlib
import re
import subprocess
import sys

KINDS = ("FUNC", "TYPE", "ENUM_TYPE", "ENUMERATOR", "FIELD", "MACRO")

# Build-infrastructure macros that are not part of the public contract
# and are therefore ignored on the header side.
INFRA_MACROS = re.compile(r"^ZLINK_.*_H_INCLUDED$|^ZLINK_EXPORT$|^ZLINK_DEFINED_STDINT$")

# Retained public macros that the formal spec documents in prose tables
# rather than inside C blocks.  They are allowed in headers even though
# the formal C-block parse does not produce them.
TABLE_ONLY_MACROS = {
    "ZLINK_DONTWAIT",
    "ZLINK_MSG_METADATA_KEY_USER_MIN",
    "ZLINK_MSG_METADATA_VALUE_MAX",
    "ZLINK_NULL",
    "ZLINK_PLAIN",
}


def parse_c_surface(text):
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    text = re.sub(r"//[^\n]*", " ", text)
    out = {kind: set() for kind in KINDS}
    for declaration in re.findall(r"ZLINK_EXPORT\s+([^;]+);", text, re.S):
        found = re.search(r"\b(zlink_[A-Za-z0-9_]+)\s*\(", declaration)
        if found:
            out["FUNC"].add(found.group(1))
    for found in re.finditer(
        r"typedef\s+enum\s+\w+\s*\{(.*?)\}\s*(zlink_\w+)\s*;", text, re.S
    ):
        out["ENUM_TYPE"].add(found.group(2))
        out["ENUMERATOR"].update(
            re.findall(r"\b(ZLINK_[A-Z0-9_]+)\s*(?==|,)", found.group(1))
        )
    for found in re.finditer(r"(?<!typedef\s)enum\s*\{(.*?)\}\s*;", text, re.S):
        out["ENUMERATOR"].update(
            re.findall(r"\b(ZLINK_[A-Z0-9_]+)\s*(?==|,)", found.group(1))
        )
    for found in re.finditer(
        r"typedef\s+struct(?:\s+\w+)?\s*\{(.*?)\}\s*(zlink_\w+)\s*;", text, re.S
    ):
        body, owner = found.group(1), found.group(2)
        out["TYPE"].add(owner)
        for declaration in body.split(";")[:-1]:
            field = re.search(
                r"([A-Za-z_]\w*)\s*(?:\[[^\]]+\])?\s*$", declaration.strip(), re.S
            )
            if field:
                out["FIELD"].add(f"{owner}.{field.group(1)}")
    without_blocks = re.sub(
        r"typedef\s+(?:enum|struct)(?:\s+\w+)?\s*\{.*?\}\s*zlink_\w+\s*;",
        " ",
        text,
        flags=re.S,
    )
    for statement in re.findall(r"\btypedef\b.*?;", without_blocks, re.S):
        found = re.search(r"\(\s*\*?\s*(zlink_\w+)\s*\)\s*\(", statement)
        if not found:
            found = re.search(r"\b(zlink_\w+)\s*;$", statement)
        if found:
            out["TYPE"].add(found.group(1))
    out["MACRO"].update(re.findall(r"^\s*#\s*define\s+(ZLINK_\w+)", text, re.M))
    return out


def header_closure(root):
    include_root = root / "core" / "include"
    seen = []
    queue = [include_root / "zlink.h"]
    visited = set()
    while queue:
        path = queue.pop()
        if path in visited or not path.exists():
            continue
        visited.add(path)
        seen.append(path)
        text = path.read_text()
        for rel in re.findall(r'#\s*include\s+["<]([^">]+)[">]', text):
            candidate = include_root / rel
            if candidate.exists():
                queue.append(candidate)
    return seen


def c_blocks(path):
    text = path.read_text()
    fence = chr(96) * 3
    return "\n".join(re.findall(rf"^{fence}c\s*\n(.*?)^{fence}\s*$", text, re.M | re.S))


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 1
    root = pathlib.Path(sys.argv[1])
    lib = pathlib.Path(sys.argv[2])
    failures = []

    spec_dir = root / "core" / "doc" / "spec" / "core"
    en_docs = sorted(
        p for p in spec_dir.rglob("*.md") if not p.name.endswith(".ko.md")
    )
    formal = {kind: set() for kind in KINDS}
    for en in en_docs:
        ko = en.with_name(en.name[: -len(".md")] + ".ko.md")
        en_c = c_blocks(en)
        if ko.exists() and c_blocks(ko) != en_c:
            failures.append(f"ko/en C block mismatch: {en.relative_to(root)}")
        parsed = parse_c_surface(en_c)
        for kind in KINDS:
            formal[kind].update(parsed[kind])

    headers = {kind: set() for kind in KINDS}
    for path in header_closure(root):
        parsed = parse_c_surface(path.read_text())
        for kind in KINDS:
            headers[kind].update(parsed[kind])
    headers["MACRO"] = {
        m for m in headers["MACRO"] if not INFRA_MACROS.match(m)
    }

    missing_funcs = formal["FUNC"] - headers["FUNC"]
    extra_funcs = headers["FUNC"] - formal["FUNC"]
    if missing_funcs:
        failures.append(f"header missing formal functions ({len(missing_funcs)}): "
                        f"{sorted(missing_funcs)[:8]} ...")
    if extra_funcs:
        failures.append(f"header declares non-formal functions ({len(extra_funcs)}): "
                        f"{sorted(extra_funcs)[:8]} ...")

    for kind in ("TYPE", "ENUM_TYPE", "ENUMERATOR", "FIELD", "MACRO"):
        missing = formal[kind] - headers[kind]
        if missing:
            failures.append(f"header missing formal {kind} ({len(missing)}): "
                            f"{sorted(missing)[:8]} ...")

    removed = json.loads(
        (root / "core" / "tests" / "contract" /
         "removed-identifiers-10.0.0.json").read_text()
    )
    for kind in KINDS:
        leftovers = set(removed.get(kind, ())) & headers[kind]
        if leftovers:
            failures.append(f"removed {kind} still declared ({len(leftovers)}): "
                            f"{sorted(leftovers)[:8]} ...")

    if lib.exists():
        nm = subprocess.run(
            ["nm", "-D", "--defined-only", str(lib)],
            capture_output=True, text=True, check=True,
        )
        exports = {
            line.split()[-1]
            for line in nm.stdout.splitlines()
            if line.split() and line.split()[-1].startswith("zlink_")
        }
        missing_exports = formal["FUNC"] - exports
        extra_exports = exports - formal["FUNC"]
        if missing_exports:
            failures.append(f"library missing formal exports ({len(missing_exports)}): "
                            f"{sorted(missing_exports)[:8]} ...")
        if extra_exports:
            failures.append(f"library exports non-formal symbols ({len(extra_exports)}): "
                            f"{sorted(extra_exports)[:8]} ...")
    else:
        failures.append(f"library not found: {lib}")

    if failures:
        print("PUBLIC SURFACE CONTRACT: FAIL")
        for failure in failures:
            print(" -", failure)
        return 1
    print("PUBLIC SURFACE CONTRACT: PASS")
    print(f"functions={len(formal['FUNC'])} exports match, "
          f"removed identifiers absent")
    return 0


if __name__ == "__main__":
    sys.exit(main())
