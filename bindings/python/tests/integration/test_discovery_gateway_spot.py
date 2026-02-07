import time
import unittest

import zlink

from .helpers import (
    transports,
    endpoint_for,
    try_transport,
    recv_with_timeout,
    gateway_send_with_retry,
    spot_recv_with_timeout,
    wait_until,
)


class DiscoveryGatewaySpotScenarioTest(unittest.TestCase):
    def test_discovery_gateway_spot_flow(self):
        ctx = zlink.Context()
        for name, endpoint in transports("discovery"):
            def run():
                suffix = str(int(time.time() * 1000))
                reg_pub = "inproc://reg-pub-python-" + suffix
                reg_router = "inproc://reg-router-python-" + suffix
                registry = zlink.Registry(ctx)
                registry.set_endpoints(reg_pub, reg_router)
                registry.start()

                discovery = zlink.Discovery(ctx, zlink.SERVICE_TYPE_GATEWAY_RECEIVER)
                discovery.connect_registry(reg_pub)
                discovery.subscribe("svc")

                provider = zlink.Receiver(ctx)
                svc_ep = endpoint_for(name, endpoint, "-svc")
                provider.bind(svc_ep)
                router = provider.router_socket()
                provider.connect_registry(reg_router)
                provider.register("svc", svc_ep, 1)
                status = -1
                for _ in range(20):
                    status, _, _ = provider.register_result("svc")
                    if status == 0:
                        break
                    time.sleep(0.05)
                self.assertEqual(status, 0)
                self.assertTrue(wait_until(lambda: discovery.receiver_count("svc") > 0, 5000))

                gateway = zlink.Gateway(ctx, discovery)
                self.assertTrue(wait_until(lambda: gateway.connection_count("svc") > 0, 5000))
                gateway_send_with_retry(gateway, "svc", [b"hello"], 0, 5000)

                rid = recv_with_timeout(router, 256, 2000)
                payload = b""
                for _ in range(3):
                    payload = recv_with_timeout(router, 256, 2000)
                    if payload.strip(b"\0") == b"hello":
                        break
                self.assertEqual(payload.strip(b"\0"), b"hello")

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

                spot.close()
                peer.close()
                node.close()
                router.close()
                provider.close()
                gateway.close()
                discovery.close()
                registry.close()

            try_transport(name, run)
        ctx.close()
