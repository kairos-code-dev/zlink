import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import time
import zlink


def main():
    with zlink.Context() as ctx:
        with zlink.SpotNode(ctx) as pub_node:
            with zlink.SpotNode(ctx) as sub_node:
                with zlink.Spot(pub_node) as pub_spot:
                    with zlink.Spot(sub_node) as sub_spot:
                        pub_node.bind("tcp://127.0.0.1:0")
                        endpoint = pub_node.last_endpoint()
                        sub_node.connect_peer(endpoint)
                        sub_spot.set_subscription(b"room:lobby")
                        deadline = time.monotonic() + 5.0
                        while time.monotonic() < deadline:
                            pub_spot.publish(b"room:lobby", [b"hello-spot"])
                            received = sub_spot.try_subscribe()
                            if received is None:
                                time.sleep(0.01)
                                continue
                            with received:
                                topic = received.topic.decode("utf-8")
                                data = received.to_bytes_list()[0].decode("utf-8")
                                print(f'[spot/recv] publish: "{topic}/{data}" \u2192 subscribe: "{topic}/{data}"')
                            return
                        raise TimeoutError("spot recv example timed out waiting for payload")


if __name__ == "__main__":
    main()
