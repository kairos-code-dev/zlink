#!/usr/bin/env python3
import argparse
import stat
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
CPP_PERF_DIR = ROOT / "bindings" / "cpp" / "perf"
CPP_NATIVE_DIR = ROOT / "bindings" / "cpp" / "native"
RUNTIME_ROOT = CPP_PERF_DIR / ".runtime"
C_STREAM_CLIENT = ROOT / "bindings" / "c" / "build" / "perf" / "perf_stream_client"

SINGLE_MAP = {
    "perf_pair": CPP_PERF_DIR / "single" / "build" / "cpp_perf_pair",
    "perf_pubsub": CPP_PERF_DIR / "single" / "build" / "cpp_perf_pubsub",
    "perf_dealer_dealer": CPP_PERF_DIR / "single" / "build" / "cpp_perf_dealer_dealer",
    "perf_dealer_router": CPP_PERF_DIR / "single" / "build" / "cpp_perf_dealer_router",
    "perf_router_router": CPP_PERF_DIR / "single" / "build" / "cpp_perf_router_router",
    "perf_spot": CPP_PERF_DIR / "single" / "build" / "cpp_perf_spot",
    "perf_spot_reqrep": CPP_PERF_DIR / "single" / "build" / "cpp_perf_spot_reqrep",
}

MULTI_SERVER_MAP = {
    "comp_src_dealer_dealer_server": CPP_PERF_DIR / "multi" / "build" / "cpp_comp_src_dealer_dealer_server",
    "comp_src_dealer_router_server": CPP_PERF_DIR / "multi" / "build" / "cpp_comp_src_dealer_router_server",
    "comp_src_router_router_server": CPP_PERF_DIR / "multi" / "build" / "cpp_comp_src_router_router_server",
    "comp_src_pubsub_server": CPP_PERF_DIR / "multi" / "build" / "cpp_comp_src_pubsub_server",
    "comp_src_spot_server": CPP_PERF_DIR / "multi" / "build" / "cpp_comp_src_spot_server",
    "comp_src_spot_sendsend_server": CPP_PERF_DIR / "multi" / "build" / "cpp_comp_src_spot_sendsend_server",
    "comp_src_spot_reqrep_server": CPP_PERF_DIR / "multi" / "build" / "cpp_comp_src_spot_reqrep_server",
    "comp_src_stream_server": CPP_PERF_DIR / "multi" / "build" / "cpp_comp_src_stream_server",
}

MULTI_CLIENT_MAP = {
    "comp_src_dealer_dealer_client": CPP_PERF_DIR / "multi" / "build" / "cpp_comp_src_dealer_dealer_client",
    "comp_src_dealer_router_client": CPP_PERF_DIR / "multi" / "build" / "cpp_comp_src_dealer_router_client",
    "comp_src_router_router_client": CPP_PERF_DIR / "multi" / "build" / "cpp_comp_src_router_router_client",
    "comp_src_pubsub_client": CPP_PERF_DIR / "multi" / "build" / "cpp_comp_src_pubsub_client",
    "comp_src_spot_client": CPP_PERF_DIR / "multi" / "build" / "cpp_comp_src_spot_client",
    "comp_src_spot_sendsend_client": CPP_PERF_DIR / "multi" / "build" / "cpp_comp_src_spot_sendsend_client",
    "comp_src_spot_reqrep_client": CPP_PERF_DIR / "multi" / "build" / "cpp_comp_src_spot_reqrep_client",
}


def native_runtime_dir() -> Path:
    if sys_platform() == "linux":
        candidates = ("linux-x86_64", "linux-x64")
    elif sys_platform() == "darwin":
        candidates = ("darwin-aarch64", "darwin-x86_64")
    elif sys_platform() == "windows":
        candidates = ("windows-x86_64", "windows-aarch64")
    else:
        candidates = ()

    for name in candidates:
        candidate = CPP_NATIVE_DIR / name
        if (candidate / runtime_library_name()).exists():
            return candidate
    raise SystemExit(
        "missing C++ native runtime under bindings/cpp/native for this platform"
    )


def sys_platform() -> str:
    import platform

    system = platform.system().lower()
    if system.startswith("linux"):
        return "linux"
    if system.startswith("darwin"):
        return "darwin"
    if system.startswith("windows"):
        return "windows"
    return system


def runtime_library_name() -> str:
    platform_name = sys_platform()
    if platform_name == "linux":
        return "libzlink.so"
    if platform_name == "darwin":
        return "libzlink.dylib"
    if platform_name == "windows":
        return "zlink.dll"
    return "libzlink.so"


def reset_dir(path: Path) -> None:
    path.mkdir(parents=True, exist_ok=True)
    for child in path.iterdir():
        if child.is_dir() and not child.is_symlink():
            for sub in child.iterdir():
                sub.unlink()
            child.rmdir()
        else:
            child.unlink()


def write_executable(path: Path, text: str) -> None:
    path.write_text(text)
    mode = path.stat().st_mode
    path.chmod(mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)


def make_symlink(path: Path, target: Path) -> None:
    path.symlink_to(target)


def server_wrapper(target: Path) -> str:
    return f"""#!/usr/bin/env bash
set -euo pipefail
if [[ $# -lt 2 ]]; then
  echo \"usage: <lib> <transport>\" >&2
  exit 1
fi
lib=\"$1\"
transport=\"$2\"
size_csv=\"${{PERF_MSG_SIZES:-64}}\"
size=\"${{size_csv%%,*}}\"
if [[ -z \"${{size}}\" ]]; then
  size=64
fi
exec \"{target}\" \"$lib\" \"$transport\" \"$size\"
"""


def client_wrapper(target: Path) -> str:
    return f"""#!/usr/bin/env bash
set -euo pipefail
if [[ $# -lt 3 ]]; then
  echo \"usage: <lib> <transport> <size> [args...]\" >&2
  exit 1
fi
lib=\"$1\"
transport=\"$2\"
size=\"$3\"
shift 3
exec \"{target}\" \"$lib\" \"$transport\" \"$size\" \"$@\"
"""


def prepare_single(runtime_bin: Path) -> None:
    for name, target in SINGLE_MAP.items():
        make_symlink(runtime_bin / name, target)


def prepare_multi(runtime_bin: Path) -> None:
    for name, target in MULTI_SERVER_MAP.items():
        write_executable(runtime_bin / name, server_wrapper(target))
    for name, target in MULTI_CLIENT_MAP.items():
        write_executable(runtime_bin / name, client_wrapper(target))
    make_symlink(runtime_bin / "perf_stream_client", C_STREAM_CLIENT)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--suite", choices=("single", "multi"), required=True)
    args = parser.parse_args()

    runtime_dir = RUNTIME_ROOT / args.suite
    runtime_bin = runtime_dir / "bin"
    runtime_lib = runtime_dir / "lib"
    runtime_dir.mkdir(parents=True, exist_ok=True)
    reset_dir(runtime_bin)
    if runtime_lib.exists() or runtime_lib.is_symlink():
        runtime_lib.unlink()
    runtime_lib.symlink_to(native_runtime_dir())

    if args.suite == "single":
        prepare_single(runtime_bin)
    else:
        prepare_multi(runtime_bin)

    print(runtime_dir)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
