import threading

import zlink
from sample_support import tcp_endpoint, wait_connected


def main():
    _, endpoint = tcp_endpoint()

    with zlink.Context() as ctx:
        with zlink.RouterSocket(ctx) as router:
            with zlink.DealerSocket(ctx) as dealer:
                with router.monitor_open(zlink.MonitorEventMask.CONNECTION_READY) as router_monitor:
                    with dealer.monitor_open(zlink.MonitorEventMask.CONNECTION_READY) as dealer_monitor:
                        dealer.set_routing_id(b"CLIENT")
                        router.bind(endpoint)
                        dealer.connect(endpoint)
                        wait_connected(router_monitor, dealer_monitor)

                request_done = threading.Event()
                reply_done = threading.Event()
                observed = {}

                def on_request(received):
                    observed["request_routing_id"] = received.routing_id
                    observed["request_payload"] = received.to_bytes_list()
                    request_done.set()

                def on_reply(received):
                    observed["reply_payload"] = received.to_bytes_list()
                    reply_done.set()

                router.on_receive(on_request)
                dealer.on_receive(on_reply)

                dealer.send(b"ping")
                if not request_done.wait(3.0):
                    raise TimeoutError("router callback did not receive a request")
                if observed["request_routing_id"] != zlink.RoutingId(b"CLIENT"):
                    raise AssertionError("unexpected dealer-router callback routing id")
                if observed["request_payload"] != [b"ping"]:
                    raise AssertionError("unexpected dealer-router callback request payload")

                router.send(observed["request_routing_id"], b"pong")
                if not reply_done.wait(3.0):
                    raise TimeoutError("dealer callback did not receive a reply")
                if observed["reply_payload"] != [b"pong"]:
                    raise AssertionError("unexpected dealer-router callback reply payload")
                print('[dealer-router/callback] send: "ping" → recv: "pong"')


if __name__ == "__main__":
    main()
