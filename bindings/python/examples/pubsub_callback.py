import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import threading
import zlink
from sample_common import tcp_endpoint, wait_connected


def main():
    port, endpoint = tcp_endpoint()
    with zlink.Context() as ctx:
        with zlink.PubSocket(ctx) as pub:
            with zlink.SubSocket(ctx) as sub:
                with pub.monitor_open(zlink.MonitorEvent.CONNECTION_READY) as pub_mon:
                    with sub.monitor_open(zlink.MonitorEvent.CONNECTION_READY) as sub_mon:
                        pub.bind(endpoint)
                        sub.connect(endpoint)
                        sub.set_subscription(b"prices")
                        wait_connected(pub_mon, sub_mon)

                done = threading.Event()
                result = {}

                def on_message(received):
                    result["topic"] = received.topic.decode("utf-8")
                    result["data"] = received.to_bytes_list()[0].decode("utf-8")
                    done.set()

                sub.on_subscribe(on_message)
                pub.publish(b"prices", b"101.25")
                if not done.wait(3.0):
                    raise TimeoutError("pubsub callback did not receive a message")
                print(f'[pubsub/callback] publish: "{result["topic"]}/{result["data"]}" \u2192 subscribe: "{result["topic"]}/{result["data"]}"')


if __name__ == "__main__":
    main()
