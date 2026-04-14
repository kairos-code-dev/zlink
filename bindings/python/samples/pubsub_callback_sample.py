import threading

import zlink
from sample_support import tcp_endpoint, wait_connected


def main():
    _, endpoint = tcp_endpoint()

    with zlink.Context() as ctx:
        with zlink.XPubSocket(ctx) as publisher:
            with zlink.SubSocket(ctx) as subscriber:
                with publisher.monitor_open(zlink.MonitorEventMask.CONNECTION_READY) as publisher_monitor:
                    with subscriber.monitor_open(zlink.MonitorEventMask.CONNECTION_READY) as subscriber_monitor:
                        publisher.bind(endpoint)
                        subscriber.connect(endpoint)
                        subscriber.set_subscription(b"prices")
                        wait_connected(publisher_monitor, subscriber_monitor)

                done = threading.Event()
                observed = {}

                def on_message(received):
                    observed["topic"] = received.topic
                    observed["payload"] = received.to_bytes_list()
                    done.set()

                subscriber.on_subscribe(on_message)
                event = publisher.receive_subscription_event()
                if not event.subscribed or event.topic != "prices":
                    raise AssertionError("unexpected subscription event")
                publisher.publish(b"prices", b"101.25")
                if not done.wait(3.0):
                    raise TimeoutError("pubsub callback did not receive a message")
                if observed["topic"] != "prices":
                    raise AssertionError("unexpected pubsub callback topic")
                if observed["payload"] != [b"101.25"]:
                    raise AssertionError("unexpected pubsub callback payload")
                print('[pubsub/callback] publish: "prices/101.25" → subscribe: "prices/101.25"')


if __name__ == "__main__":
    main()
