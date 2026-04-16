import threading
import time

import zlink
from sample_support import tcp_endpoint, wait_until, wait_spot_peer_connected


SERVICE_NAME = "sample"
TOPIC = b"room:lobby"
PAYLOAD = b"hello-spot"


def main():
    _, peer_endpoint = tcp_endpoint()
    _, service_endpoint = tcp_endpoint()

    with zlink.Context() as ctx:
        with zlink.SpotNode(ctx) as publisher_node:
            with zlink.SpotNode(ctx) as subscriber_node:
                with zlink.PubSocket(ctx) as publisher_pub:
                    with zlink.SubSocket(ctx) as publisher_sub:
                        with zlink.PubSocket(ctx) as subscriber_pub:
                            with zlink.SubSocket(ctx) as subscriber_sub:
                                with publisher_node.create_spot() as publisher:
                                    with subscriber_node.create_spot() as subscriber:
                                        publisher_pub.bind(service_endpoint)
                                        publisher_sub.connect(service_endpoint)
                                        subscriber_node.attach_pubsub(
                                            SERVICE_NAME,
                                            subscriber_pub,
                                            subscriber_sub,
                                        )
                                        publisher_node.attach_pubsub(
                                            SERVICE_NAME,
                                            publisher_pub,
                                            publisher_sub,
                                        )

                                        publisher_node.bind(peer_endpoint)
                                        subscriber_node.connect_peer(peer_endpoint)
                                        subscriber_sub.connect(service_endpoint)
                                        subscriber.set_subscription(TOPIC)
                                        wait_spot_peer_connected(subscriber_node)

                                        delivered = threading.Event()
                                        observed = {}

                                        def on_dispatch(_spot, event):
                                            if event != zlink.SpotDispatchEvent.SUBSCRIBE_READABLE:
                                                return
                                            try:
                                                with subscriber.subscribe(
                                                    flags=zlink.RecvFlags.DONT_WAIT
                                                ) as message:
                                                    observed["service_name"] = (
                                                        message.service_name
                                                    )
                                                    observed["topic"] = message.topic
                                                    observed["payload"] = (
                                                        message.single_part_or_throw().to_bytes()
                                                    )
                                            finally:
                                                delivered.set()

                                        subscriber.on_dispatch_event(on_dispatch)

                                        def publish_until_delivered():
                                            publisher.publish(
                                                SERVICE_NAME,
                                                TOPIC,
                                                [PAYLOAD],
                                            )
                                            return delivered.wait(0.05)

                                        wait_until(
                                            publish_until_delivered,
                                            description="spot dispatch callback delivery",
                                        )
                                        if observed.get("service_name") != SERVICE_NAME:
                                            raise AssertionError("unexpected spot service name")
                                        if observed.get("topic") != TOPIC.decode("utf-8"):
                                            raise AssertionError("unexpected spot topic")
                                        if observed.get("payload") != PAYLOAD:
                                            raise AssertionError("unexpected spot payload")

                                        print(
                                            '[spot/recv] service: "sample" tick: 1 '
                                            'publish: "room:lobby/hello-spot" -> '
                                            'recv: "room:lobby/hello-spot"'
                                        )


if __name__ == "__main__":
    main()
