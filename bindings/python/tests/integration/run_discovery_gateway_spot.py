import threading
import time
from pathlib import Path
import sys

import zlink

sys.path.insert(0, str(Path(__file__).resolve().parent))

from helpers import (
    endpoint_for,
    gateway_send_with_retry,
    spot_recv_with_timeout,
    transports,
    wait_until,
)


def _close_context(ctx, timeout_sec=3.0):
    t = threading.Thread(target=ctx.close, daemon=True)
    t.start()
    t.join(timeout_sec)


def _run_case(ctx, name, endpoint):
    print(f"[py-runner] case {name} start", flush=True)
    suffix = f"{int(time.time() * 1000)}-{time.perf_counter_ns()}"
    reg_pub = "inproc://reg-pub-python-" + suffix
    reg_router = "inproc://reg-router-python-" + suffix

    registry = None
    discovery = None
    receiver = None
    router = None
    gateway = None
    node = None
    peer = None
    spot = None

    try:
        print(f"[py-runner] {name}: setup", flush=True)
        registry = zlink.Registry(ctx)
        registry.set_endpoints(reg_pub, reg_router)
        registry.start()

        discovery = zlink.Discovery(ctx, zlink.SERVICE_TYPE_GATEWAY)
        discovery.connect_registry(reg_pub)
        discovery.subscribe("svc")

        receiver = zlink.Receiver(ctx)
        svc_ep = endpoint_for(name, endpoint, "-svc")
        receiver.bind(svc_ep)
        router = receiver.router_socket()
        receiver.connect_registry(reg_router)
        receiver.register("svc", svc_ep, 1)
        print(f"[py-runner] {name}: receiver registered", flush=True)

        status = -1
        for _ in range(20):
            status, _, _ = receiver.register_result("svc")
            if status == 0:
                break
            time.sleep(0.05)
        if status != 0:
            raise RuntimeError(f"register_result failed: {status}")

        if not wait_until(lambda: discovery.receiver_count("svc") > 0, 5000):
            raise RuntimeError("discovery did not observe svc receiver")

        gateway = zlink.Gateway(ctx, discovery)
        if not wait_until(lambda: gateway.connection_count("svc") > 0, 5000):
            raise RuntimeError("gateway did not connect svc")

        gateway_send_with_retry(gateway, "svc", [b"hello"], 0, 5000)
        print(f"[py-runner] {name}: gateway send ok", flush=True)

        node = zlink.SpotNode(ctx)
        spot_ep = endpoint_for(name, endpoint, "-spot")
        node.bind(spot_ep)
        node.connect_registry(reg_router)
        node.register("spot", spot_ep)
        time.sleep(0.1)

        peer = zlink.SpotNode(ctx)
        peer.connect_registry(reg_router)
        peer.connect_peer_pub(spot_ep)
        spot = zlink.Spot(peer)
        time.sleep(0.1)
        spot.subscribe("topic")
        spot.publish("topic", [b"spot-msg"], 0)

        topic, parts = spot_recv_with_timeout(spot, 5000)
        print(f"[py-runner] {name}: spot recv ok", flush=True)
        if topic != "topic":
            raise RuntimeError(f"unexpected topic: {topic!r}")
        if not parts or parts[0].strip(b"\0") != b"spot-msg":
            raise RuntimeError(f"unexpected spot payload: {parts!r}")
    finally:
        if spot is not None:
            spot.close()
        if peer is not None:
            peer.close()
        if node is not None:
            node.close()
        if router is not None:
            router.close()
        if receiver is not None:
            receiver.close()
        if gateway is not None:
            gateway.close()
        if discovery is not None:
            discovery.close()
        if registry is not None:
            registry.close()
    print(f"[py-runner] case {name} end", flush=True)


def main():
    ctx = zlink.Context()
    try:
        for name, endpoint in transports("discovery"):
            if name == "inproc":
                continue
            _run_case(ctx, name, endpoint)
    finally:
        print("[py-runner] context close", flush=True)
        _close_context(ctx)
    print("discovery_gateway_spot runner passed")


if __name__ == "__main__":
    main()
