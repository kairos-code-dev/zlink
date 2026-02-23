#!/usr/bin/env python3
import time
import sys

from bench_common import (
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


def run_mode(transport: str, size: int, len32be_dispatch: bool, pattern: str) -> int:
    warmup = parse_env("BENCH_WARMUP_COUNT", 1000)
    lat_count = parse_env("BENCH_LAT_COUNT", 500)
    msg_count = parse_env("BENCH_MSG_COUNT", 10000)
    inflight_limit = max(1, parse_env("BENCH_STREAM_INFLIGHT", 10))
    io_timeout_ms = parse_env("BENCH_STREAM_TIMEOUT_MS", 5000)
    drain_timeout_ms = parse_env("BENCH_STREAM_DRAIN_TIMEOUT_MS", max(io_timeout_ms * 6, 30000))

    ctx = zlink.Context()
    server = zlink.Socket(ctx, int(zlink.SocketType.STREAM))
    client = zlink.Socket(ctx, int(zlink.SocketType.STREAM))
    endpoint = endpoint_for(
        transport, "stream-len32be" if len32be_dispatch else "stream"
    )
    echo = StreamCallbackEcho(server, len32be_dispatch, echo=True)
    sink = StreamCallbackEcho(client, len32be_dispatch, echo=False)

    try:
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
