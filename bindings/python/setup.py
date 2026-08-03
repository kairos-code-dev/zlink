# SPDX-License-Identifier: MPL-2.0

from __future__ import annotations

import platform
import sys
import os
from pathlib import Path
from setuptools import Extension, setup


PACKAGE_ROOT = Path(__file__).resolve().parent


def _core_prefix() -> Path:
    value = os.environ.get("ZLINK_CORE_PREFIX")
    if not value:
        raise RuntimeError(
            "ZLINK_CORE_PREFIX is required; build against an explicit Core "
            "installation prefix rather than the repository build directory"
        )
    prefix = Path(value).expanduser().resolve()
    include_dir = prefix / "include"
    library_dir = prefix / ("bin" if sys.platform == "win32" else "lib")
    if not (include_dir / "zlink.h").is_file():
        raise RuntimeError(f"Core headers are missing from {include_dir}")
    if not any(
        candidate.name.startswith(("libzlink", "zlink"))
        for candidate in library_dir.glob("*")
    ):
        raise RuntimeError(f"Core library is missing from {library_dir}")
    return prefix


CORE_PREFIX = _core_prefix()
CORE_INCLUDE = CORE_PREFIX / "include"
CORE_LIBRARY = CORE_PREFIX / ("bin" if sys.platform == "win32" else "lib")


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
        include_dirs=[str(CORE_INCLUDE)],
        library_dirs=[str(CORE_LIBRARY)],
        libraries=["zlink"],
        runtime_library_dirs=_runtime_library_dirs(),
        extra_compile_args=_compile_args(),
        extra_link_args=["-pthread"] if not sys.platform == "win32" else [],
    )


def _native_package_data() -> dict[str, list[str]]:
    machine = platform.machine().lower()
    if sys.platform == "win32":
        platform_dir = (
            "windows-x86_64"
            if "64" in os.environ.get("PROCESSOR_ARCHITECTURE", "")
            else "windows-x86"
        )
    elif sys.platform == "darwin":
        platform_dir = (
            "darwin-aarch64" if machine in {"arm64", "aarch64"} else "darwin-x86_64"
        )
    elif sys.platform.startswith("linux"):
        platform_dir = (
            "linux-aarch64" if machine in {"arm64", "aarch64"} else "linux-x86_64"
        )
        payload_dir = PACKAGE_ROOT / "src" / "zlink" / "native" / platform_dir
        if not (payload_dir / "libzlink.so").exists():
            raise RuntimeError(f"Core runtime payload is missing from {payload_dir}")
        if any(payload_dir.glob("libzlink_c*")):
            raise RuntimeError(f"Obsolete libzlink_c payload remains in {payload_dir}")
    else:
        raise RuntimeError(f"Unsupported platform: {sys.platform}")
    return {"zlink": ["py.typed", f"native/{platform_dir}/*"]}


setup(
    package_data=_native_package_data(),
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
