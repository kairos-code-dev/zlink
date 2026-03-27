import threading

import zlink


def main():
    with zlink.Context() as ctx:
        with zlink.PubSocket(ctx) as publisher:
            with zlink.SubSocket(ctx) as subscriber:
                done = threading.Event()

                def on_message(received):
                    print(received.topic.decode("utf-8"), received.to_bytes_list())
                    done.set()

                endpoint = "inproc://py-example-pubsub-callback"
                publisher.bind(endpoint)
                subscriber.connect(endpoint)
                subscriber.subscribe(b"prices")
                subscriber.on_topic_message(on_message)

                publisher.publish(b"prices", b"101.25")
                if not done.wait(3.0):
                    raise TimeoutError("pubsub callback did not receive a message")


if __name__ == "__main__":
    main()
