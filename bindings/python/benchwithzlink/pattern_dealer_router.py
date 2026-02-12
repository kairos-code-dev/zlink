#!/usr/bin/env python3
import time
import sys

from bench_common import (
    endpoint_for,
    parse_env,
    parse_pattern_args,
    print_result,
    resolve_msg_count,
    settle,
    zlink,
)


def run(transport: str, size: int) -> int:
    warmup = parse_env("BENCH_WARMUP_COUNT", 1000)
    lat_count = parse_env("BENCH_LAT_COUNT", 1000)
    msg_count = resolve_msg_count(size)

    ctx = zlink.Context()
    router = zlink.Socket(ctx, int(zlink.SocketType.ROUTER))
    dealer = zlink.Socket(ctx, int(zlink.SocketType.DEALER))
    endpoint = endpoint_for(transport, "dealer-router")

    try:
        dealer.setsockopt(int(zlink.SocketOption.ROUTING_ID), b"CLIENT")
        router.bind(endpoint)
        dealer.connect(endpoint)
        settle()

        send_none = int(zlink.SendFlag.NONE)
        send_more = int(zlink.SendFlag.SNDMORE)
        recv_none = int(zlink.ReceiveFlag.NONE)
        buf = bytearray(b"a" * size)
        rid_buf = bytearray(256)
        data_buf = bytearray(max(1, size))
        rid_view = memoryview(rid_buf)

        for _ in range(warmup):
            dealer.send(buf, send_none)
            rid_len = router.recv_into(rid_buf, recv_none)
            router.recv_into(data_buf, recv_none)
            router.send(rid_view[:rid_len], send_more)
            router.send(buf, send_none)
            dealer.recv_into(data_buf, recv_none)

        start = time.perf_counter()
        for _ in range(lat_count):
            dealer.send(buf, send_none)
            rid_len = router.recv_into(rid_buf, recv_none)
            router.recv_into(data_buf, recv_none)
            router.send(rid_view[:rid_len], send_more)
            router.send(buf, send_none)
            dealer.recv_into(data_buf, recv_none)
        lat_us = ((time.perf_counter() - start) * 1_000_000.0) / (lat_count * 2)

        start = time.perf_counter()
        for _ in range(msg_count):
            dealer.send(buf, send_none)
        for _ in range(msg_count):
            router.recv_into(rid_buf, recv_none)
            router.recv_into(data_buf, recv_none)

        throughput = msg_count / (time.perf_counter() - start)
        print_result("DEALER_ROUTER", transport, size, throughput, lat_us)
        return 0
    except Exception:
        return 2
    finally:
        try:
            router.close()
            dealer.close()
            ctx.close()
        except Exception:
            pass


def main_from_args(args) -> int:
    parsed = parse_pattern_args("DEALER_ROUTER", args)
    if parsed is None:
        return 1
    transport, size = parsed
    return run(transport, size)


if __name__ == "__main__":
    raise SystemExit(main_from_args(sys.argv[1:]))
