import time
import unittest

import zlink

from .helpers import (
    transports,
    endpoint_for,
    try_transport,
    recv_with_timeout,
    send_with_retry,
    gateway_recv_with_timeout,
    spot_recv_with_timeout,
    ZLINK_SNDMORE,
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

                discovery = zlink.Discovery(ctx)
                discovery.connect_registry(reg_pub)
                discovery.subscribe("svc")

                provider = zlink.Provider(ctx)
                svc_ep = endpoint_for(name, endpoint, "-svc")
                provider.bind(svc_ep)
                provider.connect_registry(reg_router)
                provider.register("svc", svc_ep, 1)
                time.sleep(0.1)

                gateway = zlink.Gateway(ctx, discovery)
                gateway.send("svc", [b"hello"], 0)

                router = provider.router_socket()
                rid = recv_with_timeout(router, 256, 2000)
                payload = b""
                for _ in range(3):
                    payload = recv_with_timeout(router, 256, 2000)
                    if payload.strip(b"\0") == b"hello":
                        break
                self.assertEqual(payload.strip(b"\0"), b"hello")

                send_with_retry(router, rid, ZLINK_SNDMORE, 2000)
                send_with_retry(router, b"world", 0, 2000)

                service, parts = gateway_recv_with_timeout(gateway, 2000)
                self.assertEqual(service, "svc")
                self.assertEqual(parts[0].strip(b"\0"), b"world")

                node = zlink.SpotNode(ctx)
                spot_ep = endpoint_for(name, endpoint, "-spot")
                node.bind(spot_ep)
                node.connect_registry(reg_router)
                node.register("spot", spot_ep)

                peer = zlink.SpotNode(ctx)
                peer.connect_registry(reg_router)
                peer.connect_peer_pub(spot_ep)
                spot = zlink.Spot(peer)
                spot.subscribe("topic")
                spot.publish("topic", [b"spot-msg"], 0)

                topic, parts = spot_recv_with_timeout(spot, 2000)
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
