# SPDX-License-Identifier: MPL-2.0

from __future__ import annotations

import platform
import sys
from pathlib import Path
from setuptools import Extension, setup


ROOT = Path(__file__).resolve().parents[2]
CORE = ROOT / "core"
CORE_BUILD_LIB = CORE / "build" / "lib"


def _runtime_library_dirs() -> list[str]:
    # The extension is installed under zlink/_native and packaged libzlink
    # lives under zlink/native/<platform>. Keep runtime lookup package-relative
    # so built wheels do not embed source-tree paths.
    machine = platform.machine().lower()
    if sys.platform == "darwin":
        arch = "aarch64" if machine in {"arm64", "aarch64"} else "x86_64"
        return [f"@loader_path/../native/darwin-{arch}"]
    if sys.platform.startswith("linux"):
        arch = "aarch64" if machine in {"arm64", "aarch64"} else "x86_64"
        aliases = [f"$ORIGIN/../native/linux-{arch}"]
        if arch == "x86_64":
            aliases.append("$ORIGIN/../native/linux-x64")
        return aliases
    return []


def _compile_args() -> list[str]:
    if sys.platform == "win32":
        return []
    return ["-O3", "-pthread"]


def _native_extension(name: str, source: str) -> Extension:
    return Extension(
        name,
        [source],
        include_dirs=[str(CORE / "include")],
        library_dirs=[str(CORE_BUILD_LIB)],
        libraries=["zlink"],
        runtime_library_dirs=_runtime_library_dirs(),
        extra_compile_args=_compile_args(),
        extra_link_args=["-pthread"] if not sys.platform == "win32" else [],
    )


setup(
    ext_modules=[
        _native_extension(
            "zlink._native._zlink_native",
            "src/zlink/_native/_zlink_native.c",
        ),
        _native_extension(
            "zlink._native._zlink_perf_native",
            "src/zlink/_native/_zlink_perf_native.c",
        ),
    ]
)
