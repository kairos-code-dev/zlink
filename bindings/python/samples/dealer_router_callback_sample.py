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

                def on_reply(result, reply):
                    if result != zlink.RequestResult.OK:
                        raise AssertionError(f"request failed: {result}")
                    observed["reply_payload"] = [part.to_bytes() for part in reply]
                    reply_done.set()

                def on_request():
                    with router.recv() as received:
                        observed["request_routing_id"] = received.routing_id
                        observed["request_payload"] = received.to_bytes_list()
                        received.reply([b"pong"])
                        request_done.set()

                threading.Thread(target=on_request, daemon=True).start()
                dealer.request([b"ping"], on_reply, timeout=2.0)
                if not request_done.wait(3.0):
                    raise TimeoutError("router did not receive a request")
                if observed["request_routing_id"] != zlink.RoutingId(b"CLIENT"):
                    raise AssertionError("unexpected dealer-router request routing id")
                if observed["request_payload"] != [b"ping"]:
                    raise AssertionError("unexpected dealer-router request payload")
                if not reply_done.wait(3.0):
                    raise TimeoutError("dealer callback did not receive a reply")
                if observed["reply_payload"] != [b"pong"]:
                    raise AssertionError("unexpected dealer-router callback reply payload")
                print('[dealer-router/callback] request: "ping" → reply: "pong"')


if __name__ == "__main__":
    main()
