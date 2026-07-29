#!/usr/bin/env python3
"""Kill a process group as soon as an appended file marker is observable."""

from __future__ import annotations

import argparse
import ctypes
import os
import select
import signal
import struct
import time


IN_MODIFY = 0x00000002
EVENT_HEADER = struct.Struct("iIII")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--pid", type=int, required=True)
    parser.add_argument("--path", required=True)
    parser.add_argument("--marker", required=True)
    parser.add_argument("--timeout", type=float, default=60)
    args = parser.parse_args()

    deadline = time.monotonic() + args.timeout
    while not os.path.exists(args.path):
        if time.monotonic() >= deadline:
            raise SystemExit("marker file was not created")
        time.sleep(0.01)

    libc = ctypes.CDLL("libc.so.6", use_errno=True)
    descriptor = libc.inotify_init1(os.O_CLOEXEC)
    if descriptor < 0:
        raise OSError(ctypes.get_errno(), "inotify_init1 failed")
    try:
        path = os.fsencode(args.path)
        if libc.inotify_add_watch(descriptor, path, IN_MODIFY) < 0:
            raise OSError(ctypes.get_errno(), "inotify_add_watch failed")
        # Install the watch before reading existing content. This covers both
        # a marker written before startup and one appended during the read.
        with open(args.path, "r", encoding="utf-8") as stream:
            existing = stream.read()
            offset = stream.tell()
        if args.marker in existing:
            os.killpg(os.getpgid(args.pid), signal.SIGKILL)
            return
        while time.monotonic() < deadline:
            timeout = max(0, deadline - time.monotonic())
            readable, _, _ = select.select([descriptor], [], [], timeout)
            if not readable:
                break
            events = os.read(descriptor, 4096)
            cursor = 0
            while cursor + EVENT_HEADER.size <= len(events):
                _, _, _, name_length = EVENT_HEADER.unpack_from(
                    events, cursor)
                cursor += EVENT_HEADER.size + name_length
            with open(args.path, "r", encoding="utf-8") as stream:
                stream.seek(offset)
                appended = stream.read()
                offset = stream.tell()
            if args.marker not in appended:
                continue
            os.killpg(os.getpgid(args.pid), signal.SIGKILL)
            return
    finally:
        os.close(descriptor)
    raise SystemExit(f"marker was not observed: {args.marker}")


if __name__ == "__main__":
    main()
