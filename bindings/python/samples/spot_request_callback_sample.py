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
                with zlink.create_dealer_socket(ctx) as requester_dealer:
                    with zlink.create_router_socket(ctx) as responder_router:
                        responder_router.bind(endpoint)
                        requester_dealer.connect(endpoint)
                        requester_node.attach_channel_dealer_manual(
                            CHANNEL_NAME,
                            requester_dealer,
                        )

                        def respond():
                            received = zlink.create_received()
                            if not responder_router.recv_into(received):
                                raise AssertionError("expected spot request")
                            with received:
                                if received.to_bytes_list() != [REQUEST_PAYLOAD]:
                                    raise AssertionError(
                                        "unexpected spot request payload"
                                    )
                                responder_router.reply(
                                    received.routing_id,
                                    received.request_seq,
                                ).message(REPLY_PAYLOAD).submit()

                        responder_thread = threading.Thread(target=respond)
                        responder_thread.start()
                        reply_queue = queue.Queue()
                        (
                            requester.request_to_channel(CHANNEL_NAME)
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
