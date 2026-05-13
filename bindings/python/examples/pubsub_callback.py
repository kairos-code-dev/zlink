import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import zlink
from sample_common import tcp_endpoint, wait_connected


def main():
    port, endpoint = tcp_endpoint()
    with zlink.Context() as ctx:
        with zlink.PubSocket(ctx) as pub:
            with zlink.SubSocket(ctx) as sub:
                with pub.monitor_open(zlink.MonitorEventMask.CONNECTION_READY) as pub_mon:
                    with sub.monitor_open(zlink.MonitorEventMask.CONNECTION_READY) as sub_mon:
                        pub.bind(endpoint)
                        sub.connect(endpoint)
                        sub.set_subscription(b"prices")
                        wait_connected(pub_mon, sub_mon)

                pub.publish(b"prices").message(b"101.25").submit()
                with sub.subscribe() as received:
                    topic = received.topic
                    data = received.to_bytes_list()[0].decode("utf-8")
                print(f'[pubsub/recv] publish: "{topic}/{data}" \u2192 subscribe: "{topic}/{data}"')


if __name__ == "__main__":
    main()
