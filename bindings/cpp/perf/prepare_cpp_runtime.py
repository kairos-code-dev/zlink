#!/usr/bin/env python3
import argparse
import stat
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
CPP_PERF_DIR = ROOT / "bindings" / "cpp" / "perf"
RUNTIME_ROOT = CPP_PERF_DIR / ".runtime"
CORE_BUILD = ROOT / "core" / "build"

SINGLE_MAP = {
    "perf_pair": CPP_PERF_DIR / "single" / "build" / "cpp_perf_pair",
    "perf_pubsub": CPP_PERF_DIR / "single" / "build" / "cpp_perf_pubsub",
    "perf_dealer_dealer": CPP_PERF_DIR / "single" / "build" / "cpp_perf_dealer_dealer",
    "perf_dealer_router": CPP_PERF_DIR / "single" / "build" / "cpp_perf_dealer_router",
    "perf_router_router": CPP_PERF_DIR / "single" / "build" / "cpp_perf_router_router",
    "perf_spot": CPP_PERF_DIR / "single" / "build" / "cpp_perf_spot",
}

MULTI_SERVER_MAP = {
    "comp_src_dealer_dealer_server": CPP_PERF_DIR / "multi" / "build" / "cpp_comp_src_dealer_dealer_server",
    "comp_src_dealer_router_server": CPP_PERF_DIR / "multi" / "build" / "cpp_comp_src_dealer_router_server",
    "comp_src_router_router_server": CPP_PERF_DIR / "multi" / "build" / "cpp_comp_src_router_router_server",
    "comp_src_pubsub_server": CPP_PERF_DIR / "multi" / "build" / "cpp_comp_src_pubsub_server",
    "comp_src_spot_server": CPP_PERF_DIR / "multi" / "build" / "cpp_comp_src_spot_server",
    "comp_src_stream_server": CPP_PERF_DIR / "multi" / "build" / "cpp_comp_src_stream_server",
}

MULTI_CLIENT_MAP = {
    "comp_src_dealer_dealer_client": CPP_PERF_DIR / "multi" / "build" / "cpp_comp_src_dealer_dealer_client",
    "comp_src_dealer_router_client": CPP_PERF_DIR / "multi" / "build" / "cpp_comp_src_dealer_router_client",
    "comp_src_router_router_client": CPP_PERF_DIR / "multi" / "build" / "cpp_comp_src_router_router_client",
    "comp_src_pubsub_client": CPP_PERF_DIR / "multi" / "build" / "cpp_comp_src_pubsub_client",
    "comp_src_spot_client": CPP_PERF_DIR / "multi" / "build" / "cpp_comp_src_spot_client",
}


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
transport=\"$2\"
size_csv=\"${{PERF_MSG_SIZES:-64}}\"
size=\"${{size_csv%%,*}}\"
if [[ -z \"${{size}}\" ]]; then
  size=64
fi
exec \"{target}\" \"$transport\" \"$size\"
"""


def client_wrapper(target: Path) -> str:
    return f"""#!/usr/bin/env bash
set -euo pipefail
if [[ $# -lt 3 ]]; then
  echo \"usage: <lib> <transport> <size> [args...]\" >&2
  exit 1
fi
shift
transport=\"$1\"
size=\"$2\"
shift 2
exec \"{target}\" \"$transport\" \"$size\" \"$@\"
"""


def prepare_single(runtime_bin: Path) -> None:
    for name, target in SINGLE_MAP.items():
        make_symlink(runtime_bin / name, target)


def prepare_multi(runtime_bin: Path) -> None:
    for name, target in MULTI_SERVER_MAP.items():
        write_executable(runtime_bin / name, server_wrapper(target))
    for name, target in MULTI_CLIENT_MAP.items():
        write_executable(runtime_bin / name, client_wrapper(target))
    make_symlink(runtime_bin / "perf_stream_client", CORE_BUILD / "bin" / "perf_stream_client")


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
    runtime_lib.symlink_to(CORE_BUILD / "lib")

    if args.suite == "single":
        prepare_single(runtime_bin)
    else:
        prepare_multi(runtime_bin)

    print(runtime_dir)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
