import queue
import threading

import zlink

from sample_support import tcp_endpoint


CHANNEL_NAME = "orders"
REQUEST_PAYLOAD = b"spot-ping"
REPLY_PAYLOAD = b"spot-pong"


def main():
    _, endpoint = tcp_endpoint()

    with zlink.create_context() as ctx:
        with zlink.create_spot_node(ctx) as requester_node:
            with requester_node.create_spot() as requester:
                with zlink.create_router_socket(ctx) as requester_router:
                    with zlink.create_router_socket(ctx) as responder_router:
                        with requester_node.create_route_bridge() as bridge:
                            requester_rid = b"spot-request-client"
                            responder_rid = b"spot-request-server"
                            requester_router.set_routing_id(requester_rid)
                            responder_router.set_routing_id(responder_rid)
                            responder_router.bind(endpoint)
                            requester_router.connect(endpoint)
                            bridge.attach_router_channel(
                                CHANNEL_NAME,
                                requester_router,
                            )

                            def respond():
                                received = zlink.create_received()
                                if not responder_router.recv_into(received):
                                    raise AssertionError("expected spot request")
                                with received:
                                    if received.to_bytes_list()[-1:] != [REQUEST_PAYLOAD]:
                                        raise AssertionError(
                                            "unexpected spot request payload"
                                        )
                                    received.reply().message(REPLY_PAYLOAD).submit()

                            responder_thread = threading.Thread(target=respond)
                            responder_thread.start()
                            reply_queue = queue.Queue()
                            (
                                bridge.request(CHANNEL_NAME, responder_rid, requester.routing_id)
                                .message(REQUEST_PAYLOAD)
                                .timeout(2.0)
                                .submit(lambda result, parts: reply_queue.put((result, parts)))
                            )
                            result, reply = reply_queue.get(timeout=2.0)
                            if result != zlink.RequestResult.OK:
                                raise AssertionError(f"spot request failed: {result!r}")
                            try:
                                if [part.to_bytes() for part in reply] != [REPLY_PAYLOAD]:
                                    raise AssertionError("unexpected spot reply payload")
                            finally:
                                for part in reply:
                                    part.close()
                            responder_thread.join(timeout=2.0)

                            print(
                                '[spot/request/callback] request: "spot-ping" -> reply: "spot-pong"'
                            )


if __name__ == "__main__":
    main()
