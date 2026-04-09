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

                    pending_reply = asyncio.create_task(
                        dealer.request([b"ping"], timeout=2.0)
                    )
                    with await asyncio.to_thread(router.recv) as received:
                        if received.routing_id != zlink.RoutingId(b"REQ-CLIENT"):
                            raise AssertionError("unexpected request routing id")
                        msg_type, correlation_id = received.parts[0].get_request_info()
                        if msg_type != zlink.MSG_TYPE_REQUEST:
                            raise AssertionError("unexpected message type")
                        await router.reply(received.routing_id, correlation_id, [b"pong"])

                    with await pending_reply as reply:
                        if reply.to_bytes_list() != [b"pong"]:
                            raise AssertionError("unexpected reply payload")
                    print('[dealer-router/request-reply/async] send: "ping" -> recv: "pong"')
                finally:
                    dealer.close()
                    router.close()


if __name__ == "__main__":
    asyncio.run(main())
