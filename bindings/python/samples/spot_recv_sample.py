import zlink

from sample_support import tcp_endpoint, wait_until


SERVICE_NAME = "sample"
TOPIC = b"room:lobby"


def main():
    with zlink.Context() as ctx:
        _, registry_pub_endpoint = tcp_endpoint()
        _, registry_router_endpoint = tcp_endpoint()
        _, publisher_endpoint = tcp_endpoint()
        _, subscriber_endpoint = tcp_endpoint()
        with zlink.Registry(ctx) as registry:
            with zlink.Discovery(
                ctx, zlink.AutoConnectType.SPOT_MESH, SERVICE_NAME
            ) as publisher_discovery:
                with zlink.Discovery(
                    ctx, zlink.AutoConnectType.SPOT_MESH, SERVICE_NAME
                ) as subscriber_discovery:
                    with zlink.SpotNode(ctx) as publisher_node:
                        with zlink.SpotNode(ctx) as subscriber_node:
                            with publisher_node.create_spot() as publisher:
                                with subscriber_node.create_spot() as subscriber:
                                    publisher_node.set_routing_id(
                                        b"z-python-spot-recv-publisher"
                                    )
                                    subscriber_node.set_routing_id(
                                        b"a-python-spot-recv-subscriber"
                                    )
                                    publisher.set_routing_id(
                                        b"z-python-spot-recv-publisher-spot"
                                    )
                                    subscriber.set_routing_id(
                                        b"a-python-spot-recv-subscriber-spot"
                                    )
                                    registry.bind(
                                        registry_pub_endpoint,
                                        registry_router_endpoint,
                                    )
                                    registry.set_broadcast_interval(50)
                                    publisher_discovery.connect_registry(
                                        registry_router_endpoint
                                    )
                                    subscriber_discovery.connect_registry(
                                        registry_router_endpoint
                                    )
                                    publisher_node.set_pub_bind(publisher_endpoint)
                                    subscriber_node.set_pub_bind(subscriber_endpoint)
                                    publisher_node.attach_discovery(
                                        publisher_discovery
                                    )
                                    subscriber_node.attach_discovery(
                                        subscriber_discovery
                                    )
                                    subscriber.set_subscription(TOPIC)

                                    def attempt_receive():
                                        publisher_node.status_snapshot()
                                        subscriber_node.status_snapshot()
                                        subscriber_node.subjects_snapshot()
                                        publisher.publish(TOPIC).message(
                                            b"hello-spot"
                                        ).submit()
                                        received = zlink.TopicMessage()
                                        try:
                                            has_received = subscriber.subscribe_into(
                                                received,
                                                flags=zlink.RecvFlags.DONT_WAIT,
                                            )
                                        except zlink.RecvError as exc:
                                            if (
                                                exc.result != zlink.RecvResult.NO_DATA
                                                and exc.internal_errno != 2
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
                                            if received.to_bytes_list() != [
                                                b"hello-spot"
                                            ]:
                                                raise AssertionError(
                                                    "unexpected spot payload"
                                                )
                                        return True

                                    wait_until(
                                        attempt_receive,
                                        description="spot payload delivery",
                                        timeout_ms=10000,
                                    )
                                    print(
                                        '[spot/recv] service: "sample" tick: 1 '
                                        'publish: "room:lobby/hello-spot" -> '
                                        'recv: "room:lobby/hello-spot"'
                                    )


if __name__ == "__main__":
    main()
