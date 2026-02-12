#!/usr/bin/env python3
import time
import threading
import sys

from bench_common import (
    SocketWaiter,
    endpoint_for,
    gateway_send_with_retry,
    make_raw_recv_into,
    parse_env,
    parse_pattern_args,
    print_result,
    resolve_msg_count,
    settle,
    wait_until,
    zlink,
)


def run(transport: str, size: int) -> int:
    warmup = parse_env("BENCH_WARMUP_COUNT", 200)
    lat_count = parse_env("BENCH_LAT_COUNT", 200)
    msg_count = resolve_msg_count(size)

    ctx = zlink.Context()
    registry = None
    discovery = None
    receiver = None
    router = None
    gateway = None
    payload_msg = None

    try:
        suffix = str(int(time.time() * 1000))
        reg_pub = f"inproc://gw-pub-{suffix}"
        reg_router = f"inproc://gw-router-{suffix}"

        registry = zlink.Registry(ctx)
        registry.set_heartbeat(5000, 60000)
        registry.set_endpoints(reg_pub, reg_router)
        registry.start()

        discovery = zlink.Discovery(ctx, int(zlink.ServiceType.GATEWAY))
        discovery.connect_registry(reg_pub)

        service = "svc"
        discovery.subscribe(service)

        receiver = zlink.Receiver(ctx)
        provider_endpoint = endpoint_for(transport, "gateway-provider")
        receiver.bind(provider_endpoint)
        receiver.connect_registry(reg_router)
        receiver.register(service, provider_endpoint, 1)
        router = receiver.router_socket()

        gateway = zlink.Gateway(ctx, discovery)
        if not wait_until(lambda: discovery.receiver_count(service) > 0, 5000):
            return 2
        if not wait_until(lambda: gateway.connection_count(service) > 0, 5000):
            return 2

        settle()

        send_none = int(zlink.SendFlag.NONE)
        payload = b"a" * size
        payload_msg = zlink.Message.from_bytes(payload)
        parts = [payload_msg]
        rid_buf = bytearray(256)
        data_buf = bytearray(max(256, size))
        waiter = SocketWaiter(router)
        recv_none = int(zlink.ReceiveFlag.NONE)
        recv_router_rid = make_raw_recv_into(router, rid_buf)
        recv_router_data = make_raw_recv_into(router, data_buf)

        def recv_pair(waiter_obj: SocketWaiter) -> None:
            if not waiter_obj.wait(5000):
                raise RuntimeError("timeout")
            recv_router_rid(recv_none)
            if not waiter_obj.wait(5000):
                raise RuntimeError("timeout")
            recv_router_data(recv_none)

        for _ in range(warmup):
            gateway_send_with_retry(gateway, service, parts, send_none, 5000)
            recv_pair(waiter)

        start = time.perf_counter()
        for _ in range(lat_count):
            gateway_send_with_retry(gateway, service, parts, send_none, 5000)
            recv_pair(waiter)
        lat_us = ((time.perf_counter() - start) * 1_000_000.0) / lat_count

        recv_count = 0

        def receiver() -> None:
            nonlocal recv_count
            recv_waiter = SocketWaiter(router)
            for _ in range(msg_count):
                try:
                    recv_pair(recv_waiter)
                except Exception:
                    break
                recv_count += 1

        receiver_thread = threading.Thread(target=receiver)
        receiver_thread.start()

        sent = 0
        start = time.perf_counter()
        for _ in range(msg_count):
            try:
                gateway.send(service, parts, send_none)
            except Exception:
                break
            sent += 1
        receiver_thread.join()

        elapsed = time.perf_counter() - start
        effective = min(sent, recv_count)
        if effective == 0:
            print_result("GATEWAY", transport, size, 0.0, lat_us)
            return 0

        throughput = effective / elapsed if elapsed > 0 else 0.0
        print_result("GATEWAY", transport, size, throughput, lat_us)
        return 0
    except Exception:
        return 2
    finally:
        try:
            if gateway is not None:
                gateway.close()
            if router is not None:
                router.close()
            if receiver is not None:
                receiver.close()
            if discovery is not None:
                discovery.close()
            if registry is not None:
                registry.close()
            if payload_msg is not None:
                payload_msg.close()
            ctx.close()
        except Exception:
            pass


def main_from_args(args) -> int:
    parsed = parse_pattern_args("GATEWAY", args)
    if parsed is None:
        return 1
    transport, size = parsed
    return run(transport, size)


if __name__ == "__main__":
    raise SystemExit(main_from_args(sys.argv[1:]))
