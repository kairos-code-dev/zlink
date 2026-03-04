#!/usr/bin/env python3
import time

from perf_common import (
    configure_one_way_socket,
    drain_single_part,
    endpoint_for,
    parse_env,
    print_result,
    resolve_msg_count,
    settle,
    zlink,
)


def run_pair_like(pattern: str, sock_a_type: int, sock_b_type: int, transport: str, size: int) -> int:
    warmup = parse_env("PERF_WARMUP_COUNT", 1000)
    lat_count = parse_env("PERF_LAT_COUNT", 500)
    msg_count = resolve_msg_count(size)
    idle_drain_ms = parse_env("PERF_IDLE_DRAIN_MS", 500)

    ctx = zlink.Context()
    a = zlink.Socket(ctx, sock_a_type)
    b = zlink.Socket(ctx, sock_b_type)
    endpoint = endpoint_for(transport, pattern.lower())

    try:
        a.bind(endpoint)
        b.connect(endpoint)
        settle()
        configure_one_way_socket(a)
        configure_one_way_socket(b)

        send_none = int(zlink.SendFlag.NONE)
        send_dontwait = int(zlink.SendFlag.DONTWAIT)
        recv_none = int(zlink.ReceiveFlag.NONE)
        buf = b"a" * size
        recv_buf = bytearray(max(1, size))
        recv_view = memoryview(recv_buf)

        for _ in range(warmup):
            b.send(buf, send_none)
            a.recv_into(recv_buf, recv_none)

        start = time.perf_counter()
        for _ in range(lat_count):
            b.send(buf, send_none)
            n = a.recv_into(recv_buf, recv_none)
            a.send(recv_view[:n], send_none)
            b.recv_into(recv_buf, recv_none)
        lat_us = ((time.perf_counter() - start) * 1_000_000.0) / (lat_count * 2)

        start = time.perf_counter()
        sent = 0
        for _ in range(msg_count):
            try:
                b.send(buf, send_dontwait)
                sent += 1
            except Exception:
                break
        recv_count = drain_single_part(a, recv_buf, sent, idle_drain_ms)
        elapsed = time.perf_counter() - start
        effective = min(sent, recv_count)
        if effective <= 0:
            return 2
        throughput = effective / elapsed
        print_result(pattern, transport, size, throughput, lat_us)
        return 0
    except Exception:
        return 2
    finally:
        try:
            a.close()
            b.close()
            ctx.close()
        except Exception:
            pass
