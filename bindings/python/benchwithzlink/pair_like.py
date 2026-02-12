#!/usr/bin/env python3
import time

from bench_common import (
    make_cext_recv_many_into,
    make_cext_send_many_const,
    endpoint_for,
    make_raw_recv_into,
    make_raw_send_const,
    parse_env,
    print_result,
    resolve_msg_count,
    settle,
    zlink,
)


def run_pair_like(pattern: str, sock_a_type: int, sock_b_type: int, transport: str, size: int) -> int:
    warmup = parse_env("BENCH_WARMUP_COUNT", 1000)
    lat_count = parse_env("BENCH_LAT_COUNT", 500)
    msg_count = resolve_msg_count(size)

    ctx = zlink.Context()
    a = zlink.Socket(ctx, sock_a_type)
    b = zlink.Socket(ctx, sock_b_type)
    endpoint = endpoint_for(transport, pattern.lower())

    try:
        a.bind(endpoint)
        b.connect(endpoint)
        settle()

        send_none = int(zlink.SendFlag.NONE)
        recv_none = int(zlink.ReceiveFlag.NONE)
        buf = b"a" * size
        recv_buf = bytearray(max(1, size))
        recv_view = memoryview(recv_buf)
        send_b = make_raw_send_const(b, buf)
        recv_a = make_raw_recv_into(a, recv_buf)
        recv_b = make_raw_recv_into(b, recv_buf)
        send_b_many = make_cext_send_many_const(b, buf)
        recv_a_many = make_cext_recv_many_into(a, recv_buf)

        for _ in range(warmup):
            send_b(send_none)
            recv_a(recv_none)

        start = time.perf_counter()
        for _ in range(lat_count):
            send_b(send_none)
            n = recv_a(recv_none)
            a.send(recv_view[:n], send_none)
            recv_b(recv_none)
        lat_us = ((time.perf_counter() - start) * 1_000_000.0) / (lat_count * 2)

        start = time.perf_counter()
        if send_b_many is not None and recv_a_many is not None:
            send_b_many(msg_count, send_none)
            recv_a_many(msg_count, recv_none)
        else:
            for _ in range(msg_count):
                send_b(send_none)
            for _ in range(msg_count):
                recv_a(recv_none)

        throughput = msg_count / (time.perf_counter() - start)
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
