import threading

import zlink
from sample_support import tcp_endpoint, wait_until


SERVICE_NAME = "spot-svc"
TOPIC = b"room:lobby"


def main():
    with zlink.Context() as ctx:
        with zlink.SpotNode(ctx) as node:
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


if __name__ == "__main__":
    main()
