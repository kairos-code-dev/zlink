#!/usr/bin/env python3
import os
import sys

from perf_common import (
    StreamCallbackEcho,
    endpoint_for,
    int_sockopt,
    parse_env,
    parse_pattern_args,
    settle,
    zlink,
)
from perf_stream_client_common import emit_perf_lines, run_client


def run(transport: str, size: int) -> int:
    return run_mode(transport, size, len32be_dispatch=False, pattern="STREAM")


def run_len32be(transport: str, size: int) -> int:
    return run_mode(transport, size, len32be_dispatch=True, pattern="STREAM_LEN32BE")


def default_msg_count(size: int) -> int:
    if size <= 1024:
        return 10000
    if size <= 65536:
        return 100
    if size <= 131072:
        return 60
    return 40


def default_warmup_count(size: int) -> int:
    if size <= 1024:
        return 1000
    if size <= 65536:
        return 80
    if size <= 131072:
        return 50
    return 30


def default_lat_count(size: int) -> int:
    if size <= 1024:
        return 500
    if size <= 65536:
        return 80
    if size <= 131072:
        return 50
    return 30


def parse_perf_env_int(name: str, default: int) -> int:
    value = os.environ.get(name)
    if not value:
        return default
    try:
        parsed = int(value)
    except ValueError:
        return default
    return parsed if parsed > 0 else default


def run_mode(transport: str, size: int, len32be_dispatch: bool, pattern: str) -> int:
    transport = transport.lower()

    warmup = parse_env("PERF_WARMUP_COUNT", default_warmup_count(size))
    lat_count = parse_env("PERF_LAT_COUNT", default_lat_count(size))
    msg_count = parse_env("PERF_MSG_COUNT", default_msg_count(size))
    io_timeout_ms = parse_perf_env_int("PERF_STREAM_TIMEOUT_MS", 5000)

    ctx = zlink.Context()
    server = zlink.Socket(ctx, int(zlink.SocketType.STREAM))
    endpoint = endpoint_for(transport, "stream-len32be" if len32be_dispatch else "stream")
    echo = StreamCallbackEcho(server, len32be_dispatch, echo=True)

    try:
        linger_zero = int_sockopt(0)
        server.setsockopt(int(zlink.SocketOption.LINGER), linger_zero)
        server.setsockopt(int(zlink.SocketOption.SNDTIMEO), int_sockopt(io_timeout_ms))
        server.setsockopt(int(zlink.SocketOption.RCVTIMEO), int_sockopt(io_timeout_ms))
        echo.attach()
        server.bind(endpoint)
        settle()
        rc, stdout_text, stderr_text = run_client(
            [
                "--pattern",
                pattern,
                "--transport",
                transport,
                "--endpoint",
                endpoint,
                "--sizes",
                str(size),
                "--runs",
                "1",
                "--warmup-count",
                str(warmup),
                "--lat-count",
                str(lat_count),
                "--msg-count",
                str(msg_count),
                "--print-perf-result",
                "2",
            ]
        )
        emit_perf_lines(stdout_text)
        has_perf_result = any(
            line.startswith("RESULT,current,") for line in stdout_text.splitlines()
        )
        if rc != 0 or not has_perf_result:
            if stderr_text:
                print(stderr_text.strip(), file=sys.stderr, flush=True)
            return 2
        return 0
    except Exception:
        return 2
    finally:
        try:
            echo.close()
            server.close()
            ctx.close()
        except Exception:
            pass


def main_from_args(args) -> int:
    parsed = parse_pattern_args("STREAM", args)
    if parsed is None:
        return 1
    transport, size = parsed
    return run(transport, size)


if __name__ == "__main__":
    raise SystemExit(main_from_args(sys.argv[1:]))
