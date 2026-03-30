import zlink


def main():
    with zlink.Context() as ctx:
        with zlink.PubSocket(ctx) as pub:
            with zlink.SubSocket(ctx) as sub:
                endpoint = "inproc://py-example-pubsub"
                pub.bind(endpoint)
                sub.connect(endpoint)
                sub.set_subscription(b"prices")

                pub.publish(b"prices", b"101.25")
                with sub.recv() as received:
                    print(received.topic.decode("utf-8"), received.to_bytes_list())


if __name__ == "__main__":
    main()
