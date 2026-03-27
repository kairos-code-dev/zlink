import zlink


def main():
    with zlink.Context() as ctx:
        with zlink.PairSocket(ctx) as server:
            with zlink.PairSocket(ctx) as client:
                endpoint = "inproc://py-example-pair"
                server.bind(endpoint)
                client.connect(endpoint)

                client.send(b"hello")
                with server.recv_message() as received:
                    print(received.to_bytes().decode("utf-8"))


if __name__ == "__main__":
    main()
