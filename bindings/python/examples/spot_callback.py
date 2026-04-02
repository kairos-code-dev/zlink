import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import threading
import zlink


def main():
    with zlink.Context() as ctx:
        with zlink.SpotNode(ctx) as pub_node:
            with zlink.SpotNode(ctx) as sub_node:
                with zlink.Spot(pub_node) as pub_spot:
                    with zlink.Spot(sub_node) as sub_spot:
                        done = threading.Event()
                        result = {}

                        def on_message(message):
                            result["topic"] = message.topic.decode("utf-8")
                            result["data"] = message.to_bytes_list()[0].decode("utf-8")
                            done.set()

                        monitor_mask = (
                            zlink.ServiceMonitorMask.SPOT_FILTER_APPLIED
                            | zlink.ServiceMonitorMask.SPOT_SUBSCRIPTION_READY_CHANGED
                        )
                        with sub_spot.open_monitor(monitor_mask) as monitor:
                            # bind + connect BEFORE installing handlers to
                            # avoid the xattach_pipe dispatch-mode ordering
                            # issue where pipes are not registered correctly
                            # when a recv_handler is already active.
                            pub_node.bind("tcp://127.0.0.1:0")
                            endpoint = pub_node.last_endpoint()
                            sub_node.connect_peer(endpoint)
                            sub_spot.on_subscribe(on_message)
                            sub_spot.on_send_ready(lambda _: None)
                            sub_spot.set_subscription(b"room:lobby")
                            # wait for filter applied
                            while True:
                                event = monitor.recv()
                                if event.event_type == zlink.ServiceMonitorMask.SPOT_FILTER_APPLIED:
                                    break
                            # wait for subscription ready (remote peer acknowledged)
                            while True:
                                event = monitor.recv()
                                if event.event_type == zlink.ServiceMonitorMask.SPOT_SUBSCRIPTION_READY_CHANGED:
                                    break

                        pub_spot.publish(b"room:lobby", [b"hello-spot"])
                        if not done.wait(5.0):
                            raise TimeoutError("spot callback did not receive a message")
                        print(f'[spot/callback] publish: "{result["topic"]}/{result["data"]}" \u2192 subscribe: "{result["topic"]}/{result["data"]}"')


if __name__ == "__main__":
    main()
