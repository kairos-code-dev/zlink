import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import time
import zlink


def main():
    with zlink.Context() as ctx:
        with zlink.SpotNode(ctx) as pub_node:
            with zlink.SpotNode(ctx) as sub_node:
                with pub_node.create_spot() as pub_spot:
                    with sub_node.create_spot() as sub_spot:
                        pub_node.set_pub_bind("tcp://127.0.0.1:0")
                        endpoint = pub_node.last_endpoint()
                        sub_node.connect_peer(endpoint)
                        sub_spot.set_subscription(b"room:lobby")
                        deadline = time.monotonic() + 5.0
                        received = zlink.TopicMessage()
                        while time.monotonic() < deadline:
                            pub_spot.publish("room:lobby").message(b"hello-spot").submit()
                            try:
                                has_received = sub_spot.subscribe_into(
                                    received,
                                    flags=zlink.RecvFlags.DONT_WAIT
                                )
                            except zlink.RecvError as exc:
                                if exc.result != zlink.RecvResult.NO_DATA:
                                    raise
                                time.sleep(0.01)
                                continue
                            if not has_received:
                                time.sleep(0.01)
                                continue
                            with received:
                                topic = received.topic
                                data = received.to_bytes_list()[0].decode("utf-8")
                                print(f'[spot/recv] publish: "{topic}/{data}" \u2192 subscribe: "{topic}/{data}"')
                            return
                        else:
                            raise TimeoutError("spot recv example timed out waiting for payload")


if __name__ == "__main__":
    main()
