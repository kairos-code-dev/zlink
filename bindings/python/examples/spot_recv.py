import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import zlink


def main():
    with zlink.Context() as ctx:
        with zlink.SpotNode(ctx) as pub_node:
            with zlink.SpotNode(ctx) as sub_node:
                with zlink.Spot(pub_node) as pub_spot:
                    with zlink.Spot(sub_node) as sub_spot:
                        with sub_spot.open_monitor(
                            zlink.ServiceMonitorMask.SPOT_FILTER_APPLIED
                        ) as monitor:
                            pub_node.bind("tcp://127.0.0.1:0")
                            endpoint = pub_node.last_endpoint()
                            sub_node.connect_peer(endpoint)
                            sub_spot.set_subscription(b"room:lobby")
                            while True:
                                event = monitor.recv()
                                if event.event_type == zlink.ServiceMonitorMask.SPOT_FILTER_APPLIED:
                                    break

                        pub_spot.publish(b"room:lobby", [b"hello-spot"])
                        with sub_spot.subscribe() as received:
                            topic = received.topic.decode("utf-8")
                            data = received.to_bytes_list()[0].decode("utf-8")
                            print(f'[spot/recv] publish: "{topic}/{data}" \u2192 subscribe: "{topic}/{data}"')


if __name__ == "__main__":
    main()
