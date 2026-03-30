import zlink


def main():
    with zlink.Context() as ctx:
        with zlink.PairSocket(ctx) as server:
            with zlink.PairSocket(ctx) as client:
                endpoint = "inproc://py-example-pair"
                server.bind(endpoint)
                client.connect(endpoint)

                client.send(b"hello")
                with server.recv() as received:
                    print(received.to_bytes_list()[0].decode("utf-8"))


if __name__ == "__main__":
    main()
