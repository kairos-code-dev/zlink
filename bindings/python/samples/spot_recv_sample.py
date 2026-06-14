import errno

import zlink

from sample_support import tcp_endpoint, wait_spot_peer_connected, wait_until


TOPIC = "room:lobby"


def main():
    with zlink.create_context() as ctx:
        _, publisher_endpoint = tcp_endpoint()
        _, subscriber_endpoint = tcp_endpoint()
        with zlink.create_spot_node(ctx) as publisher_node:
            with zlink.create_spot_node(ctx) as subscriber_node:
                with publisher_node.create_spot() as publisher:
                    with subscriber_node.create_spot() as subscriber:
                        publisher_node.set_routing_id(b"z-python-spot-recv-publisher")
                        subscriber_node.set_routing_id(b"a-python-spot-recv-subscriber")
                        publisher.set_routing_id(b"z-python-spot-recv-publisher-spot")
                        subscriber.set_routing_id(b"a-python-spot-recv-subscriber-spot")
                        publisher_node.set_pub_bind(publisher_endpoint)
                        subscriber_node.set_pub_bind(subscriber_endpoint)
                        publisher_node.connect_peer(subscriber_endpoint)
                        subscriber_node.connect_peer(publisher_endpoint)
                        subscriber.set_subscription(TOPIC)
                        wait_spot_peer_connected(publisher_node, timeout_ms=5000)
                        wait_spot_peer_connected(subscriber_node, timeout_ms=5000)

                        def attempt_receive():
                            try:
                                with zlink.Message.from_(b"hello-spot") as message:
                                    publisher.publish(TOPIC).message(message).submit()
                            except zlink.SubmitError as exc:
                                if exc.native_errno != errno.ESHUTDOWN:
                                    raise
                                return False
                            received = zlink.create_topic_message()
                            try:
                                has_received = subscriber.subscribe_into(
                                    received,
                                    flags=zlink.RecvFlags.DONT_WAIT,
                                )
                            except zlink.RecvError as exc:
                                if (
                                    exc.result != zlink.RecvResult.NO_DATA
                                    and exc.native_errno != 2
                                ):
                                    raise
                                return False
                            if not has_received:
                                return False
                            with received:
                                if received.topic != "room:lobby":
                                    raise AssertionError(
                                        f"unexpected spot topic: {received.topic!r}"
                                    )
                                if received.to_bytes_list() != [b"hello-spot"]:
                                    raise AssertionError("unexpected spot payload")
                            return True

                        wait_until(
                            attempt_receive,
                            description="spot payload delivery",
                            timeout_ms=5000,
                        )
                        print(
                            '[spot/recv] channel: "direct" tick: 1 '
                            'publish: "room:lobby/hello-spot" -> '
                            'recv: "room:lobby/hello-spot"'
                        )


if __name__ == "__main__":
    main()
