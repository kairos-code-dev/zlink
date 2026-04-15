import zlink
from sample_support import tcp_endpoint, wait_until


SERVICE_NAME = "spot-svc"
TOPIC = b"room:lobby"


def attach_service_pair(ctx, node, service_name):
    pub_sock = zlink.PubSocket(ctx)
    sub_sock = zlink.SubSocket(ctx)
    _, endpoint = tcp_endpoint()
    pub_sock.bind(endpoint)
    sub_sock.connect(endpoint)
    node.attach_pubsub(service_name, pub_sock, sub_sock)
    return pub_sock, sub_sock


def main():
    with zlink.Context() as ctx:
        with zlink.SpotNode(ctx) as node:
            pub_sock, sub_sock = attach_service_pair(ctx, node, SERVICE_NAME)
            try:
                with node.create_spot() as spot:
                    spot.set_subscription(TOPIC)
                    observed = {}
                    published = {"done": False}

                    def attempt_receive():
                        if not published["done"]:
                            spot.publish(SERVICE_NAME, TOPIC, [b"hello-spot"])
                            published["done"] = True
                        try:
                            received = spot.subscribe(flags=zlink.RecvFlags.DONT_WAIT)
                        except zlink.RecvError as exc:
                            if exc.result != zlink.RecvResult.NO_DATA:
                                raise
                            return False
                        with received:
                            if received.topic != "room:lobby":
                                raise AssertionError(
                                    f"unexpected spot topic: {received.topic!r}"
                                )
                            if received.to_bytes_list() != [b"hello-spot"]:
                                raise AssertionError("unexpected spot payload")
                        observed["done"] = True
                        return True

                    wait_until(attempt_receive, description="spot payload delivery")
                    print('[spot/recv] publish: "room:lobby/hello-spot" → subscribe: "room:lobby/hello-spot"')
            finally:
                sub_sock.close()
                pub_sock.close()


if __name__ == "__main__":
    main()
