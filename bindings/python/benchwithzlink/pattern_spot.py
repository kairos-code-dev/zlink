#!/usr/bin/env python3
import threading
import time
import sys

from bench_common import (
    endpoint_for,
    parse_env,
    parse_pattern_args,
    print_result,
    resolve_msg_count,
    settle,
    spot_recv_with_timeout,
    zlink,
)


def run(transport: str, size: int) -> int:
    warmup = parse_env("BENCH_WARMUP_COUNT", 200)
    lat_count = parse_env("BENCH_LAT_COUNT", 200)
    msg_count = resolve_msg_count(size)
    msg_count = min(msg_count, parse_env("BENCH_SPOT_MSG_COUNT_MAX", 50000))

    ctx = zlink.Context()
    node_pub = None
    node_sub = None
    spot_pub = None
    spot_sub = None
    payload_msg = None

    try:
        node_pub = zlink.SpotNode(ctx)
        node_sub = zlink.SpotNode(ctx)

        endpoint = endpoint_for(transport, "spot")
        node_pub.bind(endpoint)
        node_sub.connect_peer_pub(endpoint)

        spot_pub = zlink.Spot(node_pub)
        spot_sub = zlink.Spot(node_sub)
        spot_sub.subscribe("bench")

        settle()

        send_none = int(zlink.SendFlag.NONE)
        payload = b"a" * size
        payload_msg = zlink.Message.from_bytes(payload)
        parts = [payload_msg]

        for _ in range(warmup):
            spot_pub.publish("bench", parts, send_none)
            spot_recv_with_timeout(spot_sub, 5000)

        start = time.perf_counter()
        for _ in range(lat_count):
            spot_pub.publish("bench", parts, send_none)
            spot_recv_with_timeout(spot_sub, 5000)
        lat_us = ((time.perf_counter() - start) * 1_000_000.0) / lat_count

        recv_count = 0

        def receiver() -> None:
            nonlocal recv_count
            for _ in range(msg_count):
                try:
                    spot_recv_with_timeout(spot_sub, 5000)
                except Exception:
                    break
                recv_count += 1

        receiver_thread = threading.Thread(target=receiver)
        receiver_thread.start()

        sent = 0
        start = time.perf_counter()
        for _ in range(msg_count):
            try:
                spot_pub.publish("bench", parts, send_none)
            except Exception:
                break
            sent += 1
        receiver_thread.join()

        elapsed = time.perf_counter() - start
        effective = min(sent, recv_count)
        if effective == 0:
            print_result("SPOT", transport, size, 0.0, lat_us)
            return 0

        throughput = effective / elapsed if elapsed > 0 else 0.0
        print_result("SPOT", transport, size, throughput, lat_us)
        return 0
    except Exception:
        return 2
    finally:
        try:
            if spot_sub is not None:
                spot_sub.close()
            if spot_pub is not None:
                spot_pub.close()
            if payload_msg is not None:
                payload_msg.close()
            if node_sub is not None:
                node_sub.close()
            if node_pub is not None:
                node_pub.close()
            ctx.close()
        except Exception:
            pass


def main_from_args(args) -> int:
    parsed = parse_pattern_args("SPOT", args)
    if parsed is None:
        return 1
    transport, size = parsed
    return run(transport, size)


if __name__ == "__main__":
    raise SystemExit(main_from_args(sys.argv[1:]))
