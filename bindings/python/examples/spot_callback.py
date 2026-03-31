import os
import sys
import threading
import uuid

import zlink


def main():
    topic = f"topic.unified.cb.{uuid.uuid4().hex}".encode("ascii")
    with zlink.Context() as ctx:
        with zlink.SpotNode(ctx) as node:
            with zlink.Spot(node) as spot:
                done = threading.Event()

                def on_message(message):
                    print(message.topic.decode("utf-8"), message.to_bytes_list())
                    done.set()

                with spot.open_monitor(
                    zlink.ServiceMonitorMask.SPOT_FILTER_APPLIED
                ) as monitor:
                    spot.set_handler(on_message)
                    spot.set_send_ready_handler(lambda _: None)
                    spot.set_subscription(topic)
                    while True:
                        event = monitor.recv()
                        if event.event_type == zlink.ServiceMonitorMask.SPOT_FILTER_APPLIED:
                            break
                    spot.publish(topic, [b"hello"])
                    if not done.wait(3.0):
                        raise TimeoutError("spot callback did not receive a message")
                    sys.stdout.flush()
                    sys.stderr.flush()
                    os._exit(0)


if __name__ == "__main__":
    main()
