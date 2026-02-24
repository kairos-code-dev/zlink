#!/usr/bin/env python3
import os
import time
import sys

from perf_common import (
    StreamCallbackEcho,
    encode_len32be_frame,
    endpoint_for,
    int_sockopt,
    parse_env,
    parse_pattern_args,
    print_result,
    settle,
    stream_send,
    stream_wait_peer_routing_id,
    zlink,
)


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
    if transport.lower() == "tcp" and size >= 65536:
        print(f"SKIP,python,{pattern},tcp,stream_large_size_guard")
        return 0

    warmup = parse_env("PERF_WARMUP_COUNT", default_warmup_count(size))
    lat_count = parse_env("PERF_LAT_COUNT", default_lat_count(size))
    msg_count = parse_env("PERF_MSG_COUNT", default_msg_count(size))
    inflight_limit = 10
    timeout_default = 15000 if size >= 65536 else 5000
    io_timeout_ms = parse_perf_env_int("PERF_STREAM_TIMEOUT_MS", timeout_default)
    drain_timeout_ms = parse_perf_env_int(
        "PERF_STREAM_DRAIN_TIMEOUT_MS", max(io_timeout_ms * 6, 30000)
    )

    ctx = zlink.Context()
    server = zlink.Socket(ctx, int(zlink.SocketType.STREAM))
    client = zlink.Socket(ctx, int(zlink.SocketType.STREAM))
    endpoint = endpoint_for(
        transport, "stream-len32be" if len32be_dispatch else "stream"
    )
    echo = StreamCallbackEcho(server, len32be_dispatch, echo=True)
    sink = StreamCallbackEcho(client, len32be_dispatch, echo=False)

    try:
        linger_zero = int_sockopt(0)
        server.setsockopt(int(zlink.SocketOption.LINGER), linger_zero)
        client.setsockopt(int(zlink.SocketOption.LINGER), linger_zero)
        server.setsockopt(int(zlink.SocketOption.SNDTIMEO), int_sockopt(io_timeout_ms))
        server.setsockopt(int(zlink.SocketOption.RCVTIMEO), int_sockopt(io_timeout_ms))
        client.setsockopt(int(zlink.SocketOption.SNDTIMEO), int_sockopt(io_timeout_ms))
        client.setsockopt(int(zlink.SocketOption.RCVTIMEO), int_sockopt(io_timeout_ms))

        echo.attach()
        sink.attach()

        server.bind(endpoint)
        client.connect(endpoint)
        settle()

        client_server_id = stream_wait_peer_routing_id(client, io_timeout_ms)

        buf = b"a" * size
        send_buf = buf if len32be_dispatch else encode_len32be_frame(buf)

        def wait_received(target: int, timeout_ms: int) -> bool:
            deadline = time.time() + (max(timeout_ms, 1) / 1000.0)
            while time.time() < deadline:
                if sink.received >= target:
                    return True
                time.sleep(0.001)
            return sink.received >= target

        expected = sink.received
        for _ in range(warmup):
            stream_send(client, client_server_id, send_buf)
            expected += 1
            if not wait_received(expected, io_timeout_ms):
                raise RuntimeError("warmup receive timeout")

        start = time.perf_counter()
        for _ in range(lat_count):
            stream_send(client, client_server_id, send_buf)
            expected += 1
            if not wait_received(expected, io_timeout_ms):
                raise RuntimeError("latency receive timeout")
        lat_us = ((time.perf_counter() - start) * 1_000_000.0) / (lat_count * 2)

        recv_begin = sink.received
        start = time.perf_counter()
        sent = 0
        for _ in range(msg_count):
            try:
                stream_send(client, client_server_id, send_buf)
            except Exception:
                break
            sent += 1
            acked = max(0, sink.received - recv_begin)
            if sent - acked >= inflight_limit:
                target = recv_begin + sent - inflight_limit + 1
                if not wait_received(target, io_timeout_ms):
                    break

        if sent > 0:
            wait_received(recv_begin + sent, drain_timeout_ms)

        elapsed = time.perf_counter() - start
        recv_delta = max(0, sink.received - recv_begin)
        effective = min(sent, recv_delta)
        throughput = (effective / elapsed) if elapsed > 0 and effective > 0 else 0.0
        print_result(pattern, transport, size, throughput, lat_us)
        return 0
    except Exception:
        return 2
    finally:
        try:
            sink.close()
            echo.close()
            server.close()
            client.close()
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
