#!/usr/bin/env python3

import pathlib
import re
import sys


def extract_contract(header: pathlib.Path) -> tuple[str, str]:
    text = header.read_text(encoding="utf-8")
    version = re.search(
        r"^#define\s+ZLINK_MONITOR_STATUS_ABI_VERSION\s+(.+)$",
        text,
        re.MULTILINE,
    )
    status = re.search(
        r"typedef struct zlink_monitor_status_t\s*\{.*?\}\s*zlink_monitor_status_t;",
        text,
        re.DOTALL,
    )
    if version is None or status is None:
        raise RuntimeError(f"monitor ABI declaration is incomplete in {header}")
    return version.group(1).strip(), re.sub(r"\s+", " ", status.group(0)).strip()


def main() -> int:
    root = pathlib.Path(sys.argv[1]).resolve()
    core_header = root / "core/include/zlink/eventing/api.h"
    binding_header = root / "bindings/c/include/zlink/eventing/api.h"
    if extract_contract(core_header) != extract_contract(binding_header):
        print(
            "socket monitor status ABI differs between Core and the C binding header",
            file=sys.stderr,
        )
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
