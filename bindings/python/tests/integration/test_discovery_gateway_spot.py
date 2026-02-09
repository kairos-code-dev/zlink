import time
import threading
import unittest

import zlink

from .helpers import (
    transports,
    endpoint_for,
    try_transport,
    gateway_send_with_retry,
    spot_recv_with_timeout,
    wait_until,
)


class DiscoveryGatewaySpotScenarioTest(unittest.TestCase):
    def test_discovery_gateway_spot_flow(self):
        ctx = zlink.Context()
        try:
            for name, endpoint in transports("discovery"):
                if name == "inproc":
                    continue
                def run():
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
                        status = -1
                        for _ in range(20):
                            status, _, _ = receiver.register_result("svc")
                            if status == 0:
                                break
                            time.sleep(0.05)
                        self.assertEqual(status, 0)
                        self.assertTrue(wait_until(lambda: discovery.receiver_count("svc") > 0, 5000))

                        gateway = zlink.Gateway(ctx, discovery)
                        self.assertTrue(wait_until(lambda: gateway.connection_count("svc") > 0, 5000))
                        gateway_send_with_retry(gateway, "svc", [b"hello"], 0, 5000)

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
                        self.assertEqual(topic, "topic")
                        self.assertEqual(parts[0].strip(b"\0"), b"spot-msg")
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

                try_transport(name, run)
        finally:
            close_context(ctx)


def close_context(ctx, timeout_sec=3.0):
    t = threading.Thread(target=ctx.close, daemon=True)
    t.start()
    t.join(timeout_sec)
