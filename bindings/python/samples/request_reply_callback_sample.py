import asyncio

import zlink

from sample_support import tcp_endpoint, wait_connected


async def main():
    _, endpoint = tcp_endpoint()

    with zlink.Context() as ctx:
        with zlink.RouterSocket(ctx) as router_socket:
            with zlink.DealerSocket(ctx) as dealer_socket:
                router = zlink.RequestRouter(router_socket)
                dealer = zlink.RequestDealer(dealer_socket)
                try:
                    with router_socket.monitor_open(
                        zlink.MonitorEvent.CONNECTION_READY
                    ) as router_monitor:
                        with dealer_socket.monitor_open(
                            zlink.MonitorEvent.CONNECTION_READY
                        ) as dealer_monitor:
                            dealer_socket.set_routing_id(b"REQ-CLIENT")
                            router_socket.bind(endpoint)
                            dealer_socket.connect(endpoint)
                            wait_connected(router_monitor, dealer_monitor)

                    reply_seen = asyncio.Event()
                    request_seen = asyncio.Event()
                    errors = []

                    def on_receive(received):
                        try:
                            if received.routing_id != zlink.RoutingId(b"REQ-CLIENT"):
                                errors.append(AssertionError("unexpected request routing id"))
                                return
                            if received.request_seq is None:
                                errors.append(AssertionError("missing request sequence"))
                                return
                            asyncio.get_running_loop().create_task(
                                router.reply(received.routing_id, received.request_seq, [b"pong"])
                            )
                        except Exception as exc:  # pragma: no cover - sample failure path
                            errors.append(exc)
                        finally:
                            request_seen.set()

                    def on_reply(received):
                        if received.to_bytes_list() != [b"pong"]:
                            errors.append(AssertionError("unexpected reply payload"))
                        reply_seen.set()

                    def on_error(exc):
                        errors.append(exc)
                        reply_seen.set()

                    router.on_receive(on_receive)
                    dealer.request_callback([b"ping"], on_reply, on_error, timeout=2.0)
                    await asyncio.wait_for(request_seen.wait(), timeout=2.0)
                    await asyncio.wait_for(reply_seen.wait(), timeout=2.0)
                    if errors:
                        raise errors[0]
                    print('[dealer-router/request-reply/callback] send: "ping" -> recv: "pong"')
                finally:
                    dealer.close()
                    router.close()


if __name__ == "__main__":
    asyncio.run(main())
