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
            with zlink.Discovery(ctx, zlink.ServiceType.SPOT, SERVICE_NAME) as discovery:
                with zlink.SpotNode(ctx) as publisher_node:
                    with zlink.SpotNode(ctx) as subscriber_node:
                        registry.bind(registry_pub_endpoint, registry_router_endpoint)
                        discovery.connect_registry(registry_router_endpoint)
                        publisher_node.attach_discovery(discovery)
                        subscriber_node.attach_discovery(discovery)
                        publisher_node.bind(publisher_endpoint)
                        subscriber_node.bind(subscriber_endpoint)
                        with publisher_node.create_spot() as publisher:
                            with subscriber_node.create_spot() as subscriber:
                                subscriber.set_subscription(TOPIC)

                                def attempt_receive():
                                    publisher.publish(
                                        SERVICE_NAME,
                                        TOPIC,
                                        [b"hello-spot"],
                                    )
                                    try:
                                        received = subscriber.subscribe(
                                            flags=zlink.RecvFlags.DONT_WAIT
                                        )
                                    except zlink.RecvError as exc:
                                        if (
                                            exc.result != zlink.RecvResult.NO_DATA
                                            and exc.internal_errno != 2
                                        ):
                                            raise
                                        return False
                                    if received is None:
                                        return False
                                    with received:
                                        if received.service_name != SERVICE_NAME:
                                            raise AssertionError(
                                                f"unexpected service: {received.service_name!r}"
                                            )
                                        if received.topic != "room:lobby":
                                            raise AssertionError(
                                                f"unexpected spot topic: {received.topic!r}"
                                            )
                                        if received.to_bytes_list() != [b"hello-spot"]:
                                            raise AssertionError("unexpected spot payload")
                                    return True

                                wait_until(
                                    attempt_receive, description="spot payload delivery"
                                )
                                print('[spot/recv] service: "sample" tick: 1 publish: "room:lobby/hello-spot" -> recv: "room:lobby/hello-spot"')


if __name__ == "__main__":
    main()
