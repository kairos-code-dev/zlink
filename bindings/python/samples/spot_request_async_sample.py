import asyncio
import threading

import zlink

from sample_support import tcp_endpoint, wait_until


SERVICE_NAME = "sample"
REQUEST_PAYLOAD = b"spot-ping"
REPLY_PAYLOAD = b"spot-pong"


async def main():
    _, endpoint = tcp_endpoint()

    with zlink.Context() as ctx:
        with zlink.SpotNode(ctx) as requester_node:
            with zlink.SpotNode(ctx) as responder_node:
                with requester_node.create_spot() as requester:
                    with responder_node.create_spot() as responder:
                        responder_node.bind(endpoint)
                        requester_node.connect_peer(endpoint)
                        handled = threading.Event()

                        def on_routed_receive(received):
                            with received:
                                if received.to_bytes_list() != [REQUEST_PAYLOAD]:
                                    raise AssertionError("unexpected spot request payload")
                                received.reply([REPLY_PAYLOAD])
                            handled.set()

                        responder.on_routed_receive(on_routed_receive)
                        wait_until(
                            lambda: requester_node.status_snapshot().connected_peer_count > 0
                            and bool(requester_node.peers_snapshot()),
                            description="spot peer connection",
                        )

                        reply = await requester.request_to_spot(
                            responder_node.routing_id,
                            responder.routing_id,
                            [REQUEST_PAYLOAD],
                            timeout=2.0,
                        )
                        try:
                            if [part.to_bytes() for part in reply] != [REPLY_PAYLOAD]:
                                raise AssertionError("unexpected spot reply payload")
                        finally:
                            for part in reply:
                                part.close()

                        if not handled.wait(2.0):
                            raise TimeoutError("spot routed receive callback did not run")
                        print('[spot/request/async] request: "spot-ping" -> reply: "spot-pong"')


if __name__ == "__main__":
    asyncio.run(main())
