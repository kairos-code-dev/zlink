#!/usr/bin/env python3
import time
import sys

from bench_common import (
    SocketWaiter,
    endpoint_for,
    int_sockopt,
    make_cext_recv_pair_drain_into,
    make_cext_recv_pair_many_into,
    make_cext_send_routed_many_const,
    make_raw_recv_into,
    make_raw_send_const,
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
        buf = b"a" * size
        id_buf = bytearray(256)
        ctrl_buf = bytearray(16)
        data_buf = bytearray(max(1, size))
        id_view = memoryview(id_buf)
        router2_send_rid = make_raw_send_const(router2, rid1)
        router2_send_ping = make_raw_send_const(router2, ping)
        router1_send_rid2 = make_raw_send_const(router1, rid2)
        router1_send_pong = make_raw_send_const(router1, pong)
        router2_send_buf = make_raw_send_const(router2, buf)
        router1_send_buf = make_raw_send_const(router1, buf)
        router1_recv_id = make_raw_recv_into(router1, id_buf)
        router1_recv_ctrl = make_raw_recv_into(router1, ctrl_buf)
        router1_recv_data = make_raw_recv_into(router1, data_buf)
        router2_recv_id = make_raw_recv_into(router2, id_buf)
        router2_recv_ctrl = make_raw_recv_into(router2, ctrl_buf)
        router2_recv_data = make_raw_recv_into(router2, data_buf)
        router2_send_pair_many = make_cext_send_routed_many_const(router2, rid1, buf)
        router1_recv_pair_many = make_cext_recv_pair_many_into(router1, id_buf, data_buf)
        router1_recv_pair_drain = make_cext_recv_pair_drain_into(router1, id_buf, data_buf)

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
                router2_send_rid(flags)
                router2_send_ping(send_dontwait)
            except Exception:
                time.sleep(0.01)
                continue

            if use_poll and not wait_for_input(router1, 0, waiter1):
                time.sleep(0.01)
                continue

            try:
                router1_recv_id(recv_dontwait)
                router1_recv_ctrl(recv_dontwait)
                connected = True
                break
            except Exception:
                time.sleep(0.01)

        if not connected:
            return 2

        router1_send_rid2(send_more)
        router1_send_pong(send_none)

        if use_poll and not wait_for_input(router2, 2000, waiter2):
            return 2

        router2_recv_id(recv_none)
        router2_recv_ctrl(recv_none)

        start = time.perf_counter()
        for _ in range(lat_count):
            router2_send_rid(send_more)
            router2_send_buf(send_none)

            if use_poll and not wait_for_input(router1, 2000, waiter1):
                return 2

            rid_len = router1_recv_id(recv_none)
            router1_recv_data(recv_none)

            router1.send(id_view[:rid_len], send_more)
            router1_send_buf(send_none)

            if use_poll and not wait_for_input(router2, 2000, waiter2):
                return 2

            router2_recv_id(recv_none)
            router2_recv_data(recv_none)

        lat_us = ((time.perf_counter() - start) * 1_000_000.0) / (lat_count * 2)

        start = time.perf_counter()
        if use_poll:
            if router2_send_pair_many is not None:
                router2_send_pair_many(msg_count, send_none)
            else:
                for _ in range(msg_count):
                    router2_send_rid(send_more)
                    router2_send_buf(send_none)

            received = 0
            while received < msg_count:
                if not wait_for_input(router1, 2000, waiter1):
                    return 2
                if router1_recv_pair_drain is not None:
                    drained = router1_recv_pair_drain(msg_count - received)
                    if drained <= 0:
                        continue
                    received += drained
                    continue

                drained = 0
                while received < msg_count:
                    try:
                        router1_recv_id(recv_dontwait)
                        router1_recv_data(recv_dontwait)
                    except Exception:
                        break
                    received += 1
                    drained += 1
                if drained <= 0:
                    continue
        elif router2_send_pair_many is not None and router1_recv_pair_many is not None:
            router2_send_pair_many(msg_count, send_none)
            router1_recv_pair_many(msg_count, recv_none)
        else:
            for _ in range(msg_count):
                router2_send_rid(send_more)
                router2_send_buf(send_none)
            for _ in range(msg_count):
                router1_recv_id(recv_none)
                router1_recv_data(recv_none)

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
