import asyncio

import zlink

from sample_support import tcp_endpoint


CHANNEL_NAME = "orders"
REQUEST_PAYLOAD = b"spot-ping"
REPLY_PAYLOAD = b"spot-pong"


async def main():
    _, endpoint = tcp_endpoint()

    with zlink.Context() as ctx:
        with zlink.SpotNode(ctx) as requester_node:
            with requester_node.create_spot() as requester:
                with zlink.DealerSocket(ctx) as requester_dealer:
                    with zlink.RouterSocket(ctx) as responder_router:
                        responder_router.bind(endpoint)
                        requester_dealer.connect(endpoint)
                        requester_node.attach_channel_dealer_manual(
                            CHANNEL_NAME,
                            requester_dealer,
                        )

                        async def respond():
                            received = responder_router.recv()
                            try:
                                if received.to_bytes_list() != [REQUEST_PAYLOAD]:
                                    raise AssertionError(
                                        "unexpected spot request payload"
                                    )
                                responder_router.reply(
                                    received.routing_id,
                                    received.request_seq,
                                    [REPLY_PAYLOAD],
                                )
                            finally:
                                received.close()

                        responder_task = asyncio.create_task(respond())
                        reply = await requester.request_channel(
                            CHANNEL_NAME,
                            [REQUEST_PAYLOAD],
                            timeout=2.0,
                        )
                        try:
                            if [part.to_bytes() for part in reply] != [REPLY_PAYLOAD]:
                                raise AssertionError("unexpected spot reply payload")
                        finally:
                            for part in reply:
                                part.close()
                        await responder_task

                        print(
                            '[spot/request/async] request: "spot-ping" -> reply: "spot-pong"'
                        )


if __name__ == "__main__":
    asyncio.run(main())
