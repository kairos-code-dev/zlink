import zlink


def main():
    with zlink.Context() as ctx:
        with zlink.RouterSocket(ctx) as router:
            with zlink.DealerSocket(ctx) as dealer:
                endpoint = "inproc://py-example-dealer-router"
                dealer.set_routing_id(b"CLIENT")
                router.bind(endpoint)
                dealer.connect(endpoint)

                dealer.send(b"ping")
                with router.recv() as request:
                    print(request.routing_id, request.to_bytes_list())
                    router.send_to(request.routing_id, b"pong")

                with dealer.recv() as response:
                    print(response.to_bytes_list())


if __name__ == "__main__":
    main()
