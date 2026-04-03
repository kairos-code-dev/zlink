import threading

import zlink
from sample_support import wait_until

def main():
    with zlink.Context() as ctx:
        with zlink.SpotNode(ctx) as pub_node:
            with zlink.SpotNode(ctx) as sub_node:
                with zlink.Spot(pub_node) as pub_spot:
                    with zlink.Spot(sub_node) as sub_spot:
                        done = threading.Event()
                        observed = {}

                        def on_message(message):
                            observed["topic"] = message.topic
                            observed["payload"] = message.to_bytes_list()
                            done.set()

                        pub_node.bind("tcp://127.0.0.1:0")
                        endpoint = pub_node.last_endpoint()
                        sub_node.connect_peer(endpoint)
                        wait_until(
                            lambda: sub_node.status_snapshot().connected_peer_count >= 1,
                            description="spot connection readiness",
                        )

                        sub_spot.set_subscription(b"room:lobby")
                        sub_spot.on_subscribe(on_message)

                        def attempt_delivery():
                            pub_spot.publish(b"room:lobby", [b"hello-spot"])
                            return done.is_set()

                        wait_until(attempt_delivery, description="spot callback delivery")
                        if observed["topic"] != b"room:lobby":
                            raise AssertionError("unexpected spot callback topic")
                        if observed["payload"] != [b"hello-spot"]:
                            raise AssertionError("unexpected spot callback payload")
                        print('[spot/callback] publish: "room:lobby/hello-spot" → subscribe: "room:lobby/hello-spot"')


if __name__ == "__main__":
    main()
