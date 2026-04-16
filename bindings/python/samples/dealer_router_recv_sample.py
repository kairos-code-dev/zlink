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

                dealer.send(b"ping")
                with router.recv() as request:
                    if request.routing_id != zlink.RoutingId.from_text("CLIENT"):
                        raise AssertionError(f"unexpected routing id: {request.routing_id!r}")
                    if request.to_bytes_list() != [b"ping"]:
                        raise AssertionError("unexpected dealer-router request payload")
                    peer_text = request.routing_id.to_text()
                    router.send(request.routing_id, b"pong")

                with dealer.recv() as reply:
                    if reply.to_bytes_list() != [b"pong"]:
                        raise AssertionError("unexpected dealer-router reply payload")
                print(f'[dealer-router/recv] peer: "{peer_text}" send: "ping" → recv: "pong"')


if __name__ == "__main__":
    main()
