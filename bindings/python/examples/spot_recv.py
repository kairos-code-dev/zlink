import os
import sys

import zlink


def main():
    with zlink.Context() as ctx:
        with zlink.Spot(ctx) as spot:
            with spot.open_monitor(
                zlink.ServiceMonitorMask.SPOT_FILTER_APPLIED
            ) as monitor:
                spot.set_send_ready_handler(lambda _: None)
                spot.set_subscription(b"room:lobby")
                while True:
                    event = monitor.recv()
                    if event.event_type == zlink.ServiceMonitorMask.SPOT_FILTER_APPLIED:
                        break
                spot.publish(b"room:lobby", [b"hello"])
                with spot.recv() as received:
                    print(received.topic.decode("utf-8"), received.to_bytes_list())
                    sys.stdout.flush()
                    sys.stderr.flush()
                    os._exit(0)


if __name__ == "__main__":
    main()
