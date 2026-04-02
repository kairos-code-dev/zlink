import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import zlink
from sample_common import tcp_endpoint, wait_connected


def main():
    port, endpoint = tcp_endpoint()
    with zlink.Context() as ctx:
        with zlink.RouterSocket(ctx) as router:
            with zlink.DealerSocket(ctx) as dealer:
                with router.open_monitor(zlink.MonitorEvent.CONNECTION_READY_CHANGED) as rtr_mon:
                    with dealer.open_monitor(zlink.MonitorEvent.CONNECTION_READY_CHANGED) as dlr_mon:
                        dealer.set_routing_id(b"CLIENT")
                        router.bind(endpoint)
                        dealer.connect(endpoint)
                        wait_connected(rtr_mon, dlr_mon)

                dealer.send(b"ping")
                with router.recv() as request:
                    router.send(b"pong", routing_id=request.routing_id)

                with dealer.recv() as response:
                    data = response.to_bytes_list()[0].decode("utf-8")
                    print(f'[dealer-router/recv] send: "ping" \u2192 recv: "{data}"')


if __name__ == "__main__":
    main()
