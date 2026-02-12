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
    msg_count = resolve_msg_count(size)

    ctx = zlink.Context()
    pub = zlink.Socket(ctx, int(zlink.SocketType.PUB))
    sub = zlink.Socket(ctx, int(zlink.SocketType.SUB))
    endpoint = endpoint_for(transport, "pubsub")

    try:
        sub.setsockopt(int(zlink.SocketOption.SUBSCRIBE), b"")
        pub.bind(endpoint)
        sub.connect(endpoint)
        settle()

        send_none = int(zlink.SendFlag.NONE)
        recv_none = int(zlink.ReceiveFlag.NONE)
        buf = bytearray(b"a" * size)
        recv_buf = bytearray(max(1, size))

        for _ in range(warmup):
            pub.send(buf, send_none)
            sub.recv_into(recv_buf, recv_none)

        start = time.perf_counter()
        for _ in range(msg_count):
            pub.send(buf, send_none)
        for _ in range(msg_count):
            sub.recv_into(recv_buf, recv_none)

        elapsed = time.perf_counter() - start
        throughput = msg_count / elapsed
        lat_us = (elapsed * 1_000_000.0) / msg_count

        print_result("PUBSUB", transport, size, throughput, lat_us)
        return 0
    except Exception:
        return 2
    finally:
        try:
            pub.close()
            sub.close()
            ctx.close()
        except Exception:
            pass


def main_from_args(args) -> int:
    parsed = parse_pattern_args("PUBSUB", args)
    if parsed is None:
        return 1
    transport, size = parsed
    return run(transport, size)


if __name__ == "__main__":
    raise SystemExit(main_from_args(sys.argv[1:]))
