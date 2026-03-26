import zlink


def main():
    with zlink.Context() as ctx:
        with zlink.Socket(ctx, zlink.SocketType.ROUTER) as router:
            with zlink.Socket(ctx, zlink.SocketType.DEALER) as dealer:
                endpoint = "inproc://py-example-dealer-router"
                dealer.set_routing_id(b"CLIENT")
                router.bind(endpoint)
                dealer.connect(endpoint)

                dealer.send(b"ping")
                with router.recv_message() as request:
                    print(request.routing_id, request.to_bytes())
                    router.send(request.routing_id, zlink.SendFlag.SNDMORE)
                router.send(b"pong")

                with dealer.recv_message() as response:
                    print(response.to_bytes())


if __name__ == "__main__":
    main()
