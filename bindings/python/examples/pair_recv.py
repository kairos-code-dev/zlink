import zlink


def main():
    with zlink.Context() as ctx:
        with zlink.Socket(ctx, zlink.SocketType.PAIR) as server:
            with zlink.Socket(ctx, zlink.SocketType.PAIR) as client:
                endpoint = "inproc://py-example-pair"
                server.bind(endpoint)
                client.connect(endpoint)

                client.send(b"hello")
                with server.recv_message() as received:
                    print(received.to_bytes().decode("utf-8"))


if __name__ == "__main__":
    main()
