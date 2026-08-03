# SPDX-License-Identifier: MPL-2.0

import ctypes
import os
import pathlib


def load_native_library(bind=None):
    candidates = []
    path = os.environ.get("ZLINK_LIBRARY_PATH")
    if path:
        candidates.append(path)
    else:
        for candidate in (
            _find_bundled_library(),
            _find_prefix_library(),
        ):
            if candidate and candidate not in candidates:
                candidates.append(candidate)
    if not candidates:
        raise OSError("zlink native library not found")

    last_error = None
    for candidate in candidates:
        try:
            if os.name == "nt":
                _prepare_windows_runtime(candidate)
            lib = ctypes.CDLL(candidate)
            if bind is not None:
                bind(lib)
            return lib
        except (AttributeError, OSError) as exc:
            last_error = exc
    raise OSError("zlink native library not found or incompatible") from last_error


def _find_bundled_library():
    base = pathlib.Path(__file__).resolve().parent.parent
    os_name = os.name
    if os_name == "nt":
        os_dir = (
            "windows-x86_64"
            if "64" in os.environ.get("PROCESSOR_ARCHITECTURE", "")
            else "windows-x86"
        )
        name = "zlink.dll"
    else:
        uname = os.uname().sysname.lower()
        if "darwin" in uname or "mac" in uname:
            os_dir = (
                "darwin-aarch64"
                if os.uname().machine in ("arm64", "aarch64")
                else "darwin-x86_64"
            )
            name = "libzlink.dylib"
        else:
            os_dir = (
                "linux-aarch64"
                if os.uname().machine in ("arm64", "aarch64")
                else "linux-x86_64"
            )
            name = "libzlink.so"
    candidate = base / "native" / os_dir / name
    if candidate.exists():
        return str(candidate)
    return None


def _find_prefix_library():
    prefix_value = os.environ.get("ZLINK_CORE_PREFIX")
    if not prefix_value:
        return None

    prefix = pathlib.Path(prefix_value).expanduser()
    candidates = []
    if os.name == "nt":
        candidates.append(prefix / "bin" / "zlink.dll")
    else:
        uname = os.uname().sysname.lower()
        if "darwin" in uname or "mac" in uname:
            candidates.append(prefix / "lib" / "libzlink.dylib")
        else:
            candidates.append(prefix / "lib" / "libzlink.so")
    for candidate in candidates:
        if candidate.exists():
            return str(candidate)
    return None


def _prepare_windows_runtime(lib_path):
    dep_names = ["libcrypto-3-x64.dll", "libssl-3-x64.dll"]
    search_dirs = []

    lib_dir = pathlib.Path(lib_path).resolve().parent
    search_dirs.append(lib_dir)

    for env_key in ("ZLINK_OPENSSL_BIN", "OPENSSL_BIN"):
        value = os.environ.get(env_key)
        if value:
            search_dirs.append(pathlib.Path(value))

    for entry in os.environ.get("PATH", "").split(";"):
        if entry:
            search_dirs.append(pathlib.Path(entry))

    search_dirs.extend(
        [
            pathlib.Path(r"C:\Program Files\OpenSSL-Win64\bin"),
            pathlib.Path(r"C:\Program Files\Git\mingw64\bin"),
            pathlib.Path(
                r"C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\TeamFoundation\Team Explorer\Git\mingw64\bin"
            ),
        ]
    )

    seen = set()
    for directory in search_dirs:
        text = str(directory)
        if not text or text in seen or not directory.exists():
            continue
        seen.add(text)
        os.add_dll_directory(text)
        for dep_name in dep_names:
            dep_path = directory / dep_name
            if dep_path.exists():
                try:
                    ctypes.CDLL(str(dep_path))
                except OSError:
                    pass
