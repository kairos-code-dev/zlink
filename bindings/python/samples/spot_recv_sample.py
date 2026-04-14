import zlink
from sample_support import wait_spot_peer_connected, wait_until

def main():
    with zlink.Context() as ctx:
        with zlink.SpotNode(ctx) as pub_node:
            with zlink.SpotNode(ctx) as sub_node:
                with pub_node.create_spot() as pub_spot:
                    with sub_node.create_spot() as sub_spot:
                        pub_node.bind("tcp://127.0.0.1:0")
                        endpoint = pub_node.last_endpoint()
                        sub_node.connect_peer(endpoint)
                        wait_spot_peer_connected(sub_node)

                        sub_spot.set_subscription(b"room:lobby")
                        observed = {}

                        def attempt_receive():
                            pub_spot.publish(b"room:lobby", [b"hello-spot"])
                            try:
                                received = sub_spot.subscribe(
                                    flags=zlink.RecvFlags.DONT_WAIT
                                )
                            except zlink.RecvError as exc:
                                if exc.result != zlink.RecvResult.NO_DATA:
                                    raise
                                return False
                            with received:
                                if received.topic != "room:lobby":
                                    raise AssertionError(f"unexpected spot topic: {received.topic!r}")
                                if received.to_bytes_list() != [b"hello-spot"]:
                                    raise AssertionError("unexpected spot payload")
                            observed["done"] = True
                            return True

                        wait_until(attempt_receive, description="spot payload delivery")
                        print('[spot/recv] publish: "room:lobby/hello-spot" → subscribe: "room:lobby/hello-spot"')


if __name__ == "__main__":
    main()
