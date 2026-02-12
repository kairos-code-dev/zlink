#!/usr/bin/env python3
import platform
from pathlib import Path

from setuptools import Extension, setup


HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
CORE_INCLUDE = ROOT / "core" / "include"
PY_NATIVE_ROOT = ROOT / "bindings" / "python" / "src" / "zlink" / "native"


def native_dir() -> Path:
    sys_name = platform.system().lower()
    machine = platform.machine().lower()
    if "windows" in sys_name:
        arch = "x86_64" if machine in ("x86_64", "amd64") else "aarch64"
        return PY_NATIVE_ROOT / f"windows-{arch}"
    if "darwin" in sys_name:
        arch = "aarch64" if machine in ("arm64", "aarch64") else "x86_64"
        return PY_NATIVE_ROOT / f"darwin-{arch}"
    arch = "aarch64" if machine in ("arm64", "aarch64") else "x86_64"
    return PY_NATIVE_ROOT / f"linux-{arch}"


LIB_DIR = native_dir()
if not LIB_DIR.exists():
    raise SystemExit(f"Native lib directory not found: {LIB_DIR}")

ext_kwargs = dict(
    name="_zlink_fastpath",
    sources=[str(HERE / "_zlink_fastpath.c")],
    include_dirs=[str(CORE_INCLUDE)],
    library_dirs=[str(LIB_DIR)],
    libraries=["zlink"],
    extra_compile_args=["-O3"],
)

if platform.system().lower() != "windows":
    ext_kwargs["runtime_library_dirs"] = [str(LIB_DIR)]

ext_modules = [Extension(**ext_kwargs)]

setup(
    name="zlink-fastpath-bench-poc",
    version="0.0.1",
    ext_modules=ext_modules,
)
