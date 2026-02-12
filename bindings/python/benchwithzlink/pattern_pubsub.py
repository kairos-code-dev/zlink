#!/usr/bin/env python3
import time
import sys

from bench_common import (
    make_cext_recv_many_into,
    make_cext_send_many_const,
    endpoint_for,
    make_raw_recv_into,
    make_raw_send_const,
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
        buf = b"a" * size
        recv_buf = bytearray(max(1, size))
        send_pub = make_raw_send_const(pub, buf)
        recv_sub = make_raw_recv_into(sub, recv_buf)
        send_pub_many = make_cext_send_many_const(pub, buf)
        recv_sub_many = make_cext_recv_many_into(sub, recv_buf)

        for _ in range(warmup):
            send_pub(send_none)
            recv_sub(recv_none)

        start = time.perf_counter()
        if send_pub_many is not None and recv_sub_many is not None:
            send_pub_many(msg_count, send_none)
            recv_sub_many(msg_count, recv_none)
        else:
            for _ in range(msg_count):
                send_pub(send_none)
            for _ in range(msg_count):
                recv_sub(recv_none)

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
