#!/usr/bin/env python3
import time
import sys

from bench_common import (
    SocketWaiter,
    endpoint_for,
    int_sockopt,
    parse_env,
    parse_pattern_args,
    print_result,
    resolve_msg_count,
    settle,
    wait_for_input,
    zlink,
)


def run_router_router(transport: str, size: int, use_poll: bool) -> int:
    lat_count = parse_env("BENCH_LAT_COUNT", 1000)
    msg_count = resolve_msg_count(size)

    ctx = zlink.Context()
    router1 = zlink.Socket(ctx, int(zlink.SocketType.ROUTER))
    router2 = zlink.Socket(ctx, int(zlink.SocketType.ROUTER))
    endpoint = endpoint_for(transport, "router-router")

    try:
        send_none = int(zlink.SendFlag.NONE)
        send_more = int(zlink.SendFlag.SNDMORE)
        send_dontwait = int(zlink.SendFlag.DONTWAIT)
        recv_none = int(zlink.ReceiveFlag.NONE)
        recv_dontwait = int(zlink.ReceiveFlag.DONTWAIT)
        rid1 = b"ROUTER1"
        rid2 = b"ROUTER2"
        ping = b"PING"
        pong = b"PONG"
        buf = bytearray(b"a" * size)
        id_buf = bytearray(256)
        ctrl_buf = bytearray(16)
        data_buf = bytearray(max(1, size))
        id_view = memoryview(id_buf)

        router1.setsockopt(int(zlink.SocketOption.ROUTING_ID), b"ROUTER1")
        router2.setsockopt(int(zlink.SocketOption.ROUTING_ID), b"ROUTER2")
        router1.setsockopt(int(zlink.SocketOption.ROUTER_MANDATORY), int_sockopt(1))
        router2.setsockopt(int(zlink.SocketOption.ROUTER_MANDATORY), int_sockopt(1))
        router1.bind(endpoint)
        router2.connect(endpoint)
        settle()

        waiter1 = SocketWaiter(router1)
        waiter2 = SocketWaiter(router2)

        connected = False
        for _ in range(100):
            try:
                flags = send_more | send_dontwait
                router2.send_const(rid1, flags)
                router2.send_const(ping, send_dontwait)
            except Exception:
                time.sleep(0.01)
                continue

            if use_poll and not wait_for_input(router1, 0, waiter1):
                time.sleep(0.01)
                continue

            try:
                router1.recv_into(id_buf, recv_dontwait)
                router1.recv_into(ctrl_buf, recv_dontwait)
                connected = True
                break
            except Exception:
                time.sleep(0.01)

        if not connected:
            return 2

        router1.send_const(rid2, send_more)
        router1.send_const(pong, send_none)

        if use_poll and not wait_for_input(router2, 2000, waiter2):
            return 2

        router2.recv_into(id_buf, recv_none)
        router2.recv_into(ctrl_buf, recv_none)

        start = time.perf_counter()
        for _ in range(lat_count):
            router2.send_const(rid1, send_more)
            router2.send(buf, send_none)

            if use_poll and not wait_for_input(router1, 2000, waiter1):
                return 2

            rid_len = router1.recv_into(id_buf, recv_none)
            router1.recv_into(data_buf, recv_none)

            router1.send(id_view[:rid_len], send_more)
            router1.send(buf, send_none)

            if use_poll and not wait_for_input(router2, 2000, waiter2):
                return 2

            router2.recv_into(id_buf, recv_none)
            router2.recv_into(data_buf, recv_none)

        lat_us = ((time.perf_counter() - start) * 1_000_000.0) / (lat_count * 2)

        start = time.perf_counter()
        for _ in range(msg_count):
            router2.send_const(rid1, send_more)
            router2.send(buf, send_none)
        for _ in range(msg_count):
            if use_poll and not wait_for_input(router1, 2000, waiter1):
                return 2
            router1.recv_into(id_buf, recv_none)
            router1.recv_into(data_buf, recv_none)

        throughput = msg_count / (time.perf_counter() - start)
        pattern = "ROUTER_ROUTER_POLL" if use_poll else "ROUTER_ROUTER"
        print_result(pattern, transport, size, throughput, lat_us)
        return 0
    except Exception:
        return 2
    finally:
        try:
            router1.close()
            router2.close()
            ctx.close()
        except Exception:
            pass


def run(transport: str, size: int) -> int:
    return run_router_router(transport, size, False)


def main_from_args(args) -> int:
    parsed = parse_pattern_args("ROUTER_ROUTER", args)
    if parsed is None:
        return 1
    transport, size = parsed
    return run(transport, size)


if __name__ == "__main__":
    raise SystemExit(main_from_args(sys.argv[1:]))
