#!/usr/bin/env python3
import time
import sys

from perf_common import (
    configure_one_way_socket,
    drain_single_part,
    endpoint_for,
    parse_env,
    parse_pattern_args,
    print_result,
    resolve_msg_count,
    settle,
    zlink,
)


def run(transport: str, size: int) -> int:
    warmup = parse_env("PERF_WARMUP_COUNT", 1000)
    msg_count = resolve_msg_count(size)
    idle_drain_ms = parse_env("PERF_IDLE_DRAIN_MS", 500)

    ctx = zlink.Context()
    pub = zlink.Socket(ctx, int(zlink.SocketType.PUB))
    sub = zlink.Socket(ctx, int(zlink.SocketType.SUB))
    endpoint = endpoint_for(transport, "pubsub")

    try:
        sub.setsockopt(int(zlink.SocketOption.SUBSCRIBE), b"")
        pub.bind(endpoint)
        sub.connect(endpoint)
        settle()
        configure_one_way_socket(pub)
        configure_one_way_socket(sub)

        send_none = int(zlink.SendFlag.NONE)
        send_dontwait = int(zlink.SendFlag.DONTWAIT)
        recv_none = int(zlink.ReceiveFlag.NONE)
        buf = b"a" * size
        recv_buf = bytearray(max(1, size))

        for _ in range(warmup):
            pub.send(buf, send_none)
            sub.recv_into(recv_buf, recv_none)

        start = time.perf_counter()
        sent = 0
        for _ in range(msg_count):
            try:
                pub.send(buf, send_dontwait)
                sent += 1
            except Exception:
                break
        recv_count = drain_single_part(sub, recv_buf, sent, idle_drain_ms)

        elapsed = time.perf_counter() - start
        if recv_count <= 0:
            return 2
        throughput = recv_count / elapsed
        lat_us = (elapsed * 1_000_000.0) / recv_count

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
