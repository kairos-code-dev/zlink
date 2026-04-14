import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import threading
import zlink
from sample_common import tcp_endpoint, wait_connected


def main():
    port, endpoint = tcp_endpoint()
    with zlink.Context() as ctx:
        with zlink.RouterSocket(ctx) as router:
            with zlink.DealerSocket(ctx) as dealer:
                with router.monitor_open(zlink.MonitorEventMask.CONNECTION_READY) as rtr_mon:
                    with dealer.monitor_open(zlink.MonitorEventMask.CONNECTION_READY) as dlr_mon:
                        dealer.set_routing_id(b"CLIENT")
                        router.bind(endpoint)
                        dealer.connect(endpoint)
                        wait_connected(rtr_mon, dlr_mon)

                request_done = threading.Event()
                reply_done = threading.Event()
                req = {}
                rep = {}

                def on_request(received):
                    req["rid"] = received.routing_id
                    request_done.set()

                def on_reply(received):
                    rep["data"] = received.to_bytes_list()[0].decode("utf-8")
                    reply_done.set()

                router.on_receive(on_request)
                dealer.on_receive(on_reply)

                dealer.send(b"ping")
                if not request_done.wait(3.0):
                    raise TimeoutError("router callback did not receive request")

                router.send(b"pong", routing_id=req["rid"])
                if not reply_done.wait(3.0):
                    raise TimeoutError("dealer callback did not receive reply")
                print(f'[dealer-router/callback] send: "ping" \u2192 recv: "{rep["data"]}"')


if __name__ == "__main__":
    main()
