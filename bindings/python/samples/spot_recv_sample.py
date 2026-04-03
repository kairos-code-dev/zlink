import zlink
from sample_support import wait_until

def main():
    with zlink.Context() as ctx:
        with zlink.SpotNode(ctx) as pub_node:
            with zlink.SpotNode(ctx) as sub_node:
                with zlink.Spot(pub_node) as pub_spot:
                    with zlink.Spot(sub_node) as sub_spot:
                        pub_node.bind("tcp://127.0.0.1:0")
                        endpoint = pub_node.last_endpoint()
                        sub_node.connect_peer(endpoint)
                        wait_until(
                            lambda: sub_node.status_snapshot().connected_peer_count >= 1,
                            description="spot connection readiness",
                        )

                        sub_spot.set_subscription(b"room:lobby")
                        observed = {}

                        def attempt_receive():
                            pub_spot.publish(b"room:lobby", [b"hello-spot"])
                            received = sub_spot.try_subscribe()
                            if received is None:
                                return False
                            with received:
                                if received.topic != b"room:lobby":
                                    raise AssertionError(f"unexpected spot topic: {received.topic!r}")
                                if received.to_bytes_list() != [b"hello-spot"]:
                                    raise AssertionError("unexpected spot payload")
                            observed["done"] = True
                            return True

                        wait_until(attempt_receive, description="spot payload delivery")
                        print('[spot/recv] publish: "room:lobby/hello-spot" → subscribe: "room:lobby/hello-spot"')


if __name__ == "__main__":
    main()
