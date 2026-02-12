#!/usr/bin/env python3
import time
import sys

from bench_common import (
    endpoint_for,
    int_sockopt,
    parse_env,
    parse_pattern_args,
    print_result,
    resolve_msg_count,
    settle,
    stream_expect_connect_event,
    stream_recv_into,
    stream_send,
    zlink,
)


def run(transport: str, size: int) -> int:
    warmup = parse_env("BENCH_WARMUP_COUNT", 1000)
    lat_count = parse_env("BENCH_LAT_COUNT", 500)
    msg_count = resolve_msg_count(size)
    io_timeout_ms = parse_env("BENCH_STREAM_TIMEOUT_MS", 5000)

    ctx = zlink.Context()
    server = zlink.Socket(ctx, int(zlink.SocketType.STREAM))
    client = zlink.Socket(ctx, int(zlink.SocketType.STREAM))
    endpoint = endpoint_for(transport, "stream")

    try:
        server.setsockopt(int(zlink.SocketOption.SNDTIMEO), int_sockopt(io_timeout_ms))
        server.setsockopt(int(zlink.SocketOption.RCVTIMEO), int_sockopt(io_timeout_ms))
        client.setsockopt(int(zlink.SocketOption.SNDTIMEO), int_sockopt(io_timeout_ms))
        client.setsockopt(int(zlink.SocketOption.RCVTIMEO), int_sockopt(io_timeout_ms))

        server.bind(endpoint)
        client.connect(endpoint)
        settle()

        server_client_id = stream_expect_connect_event(server)
        client_server_id = stream_expect_connect_event(client)

        buf = bytearray(b"a" * size)
        recv_cap = max(256, size)
        server_rid = bytearray(256)
        server_data = bytearray(recv_cap)
        client_rid = bytearray(256)
        client_data = bytearray(recv_cap)
        server_data_view = memoryview(server_data)

        for _ in range(warmup):
            stream_send(client, client_server_id, buf)
            stream_recv_into(server, server_rid, server_data)

        start = time.perf_counter()
        for _ in range(lat_count):
            stream_send(client, client_server_id, buf)
            _, received_len = stream_recv_into(server, server_rid, server_data)
            stream_send(server, server_client_id, server_data_view[:received_len])
            stream_recv_into(client, client_rid, client_data)
        lat_us = ((time.perf_counter() - start) * 1_000_000.0) / (lat_count * 2)

        start = time.perf_counter()
        for _ in range(msg_count):
            stream_send(client, client_server_id, buf)
        for _ in range(msg_count):
            stream_recv_into(server, server_rid, server_data)

        throughput = msg_count / (time.perf_counter() - start)
        print_result("STREAM", transport, size, throughput, lat_us)
        return 0
    except Exception:
        return 2
    finally:
        try:
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
