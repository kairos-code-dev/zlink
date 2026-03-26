import zlink


def main():
    with zlink.Context() as ctx:
        with zlink.Socket(ctx, zlink.SocketType.PUB) as pub:
            with zlink.Socket(ctx, zlink.SocketType.SUB) as sub:
                endpoint = "inproc://py-example-pubsub"
                pub.bind(endpoint)
                sub.connect(endpoint)
                sub.subscribe(b"prices")

                pub.publish(b"prices", b"101.25")
                with sub.recv_topic_message() as received:
                    print(received.topic.decode("utf-8"), received.to_bytes_list())


if __name__ == "__main__":
    main()
