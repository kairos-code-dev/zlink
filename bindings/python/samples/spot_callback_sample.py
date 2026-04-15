import threading

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
                    done = threading.Event()
                    observed = {}

                    def on_dispatch(_spot, event):
                        try:
                            with spot.subscribe(flags=zlink.RecvFlags.DONT_WAIT) as message:
                                observed["topic"] = message.topic
                                observed["payload"] = message.to_bytes_list()
                        finally:
                            done.set()

                    spot.set_subscription(TOPIC)
                    spot.on_dispatch_event(on_dispatch)

                    def attempt_delivery():
                        try:
                            spot.publish(SERVICE_NAME, TOPIC, [b"hello-spot"])
                        except zlink.SubmitError:
                            return False
                        return done.is_set()

                    wait_until(attempt_delivery, description="spot callback delivery")
                    if observed["topic"] != "room:lobby":
                        raise AssertionError("unexpected spot topic")
                    if observed["payload"] != [b"hello-spot"]:
                        raise AssertionError("unexpected spot payload")
                    print('[spot/callback] publish: "room:lobby/hello-spot" → subscribe: "room:lobby/hello-spot"')
            finally:
                sub_sock.close()
                pub_sock.close()


if __name__ == "__main__":
    main()
