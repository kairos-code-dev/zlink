import os
import sys
import threading
import time
import uuid

import zlink

from perf_common import (
    latency_us_from_message,
    new_payload,
    parse_single_args,
    print_result_lines,
    result_metrics,
    stamp_payload,
)


def main(argv=None):
    args = parse_single_args(argv or sys.argv[1:], pattern="spot")
    payload = new_payload(args.msg_size)
    latencies = []
    active = False
    active_count = 0
    topic = f"bench.{uuid.uuid4().hex}".encode("ascii")
    done = threading.Event()

    with zlink.Context() as ctx:
        with zlink.Spot(ctx) as spot:
            def on_message(received):
                nonlocal active_count
                part = received.to_bytes_list()[0]
                if active:
                    latencies.append(latency_us_from_message(part))
                    active_count += 1
                    done.set()

            with spot.open_monitor(
                zlink.ServiceMonitorMask.SPOT_FILTER_APPLIED
            ) as monitor:
                spot.set_handler(on_message)
                spot.set_send_ready_handler(lambda _: None)
                spot.set_subscription(topic)
                while True:
                    event = monitor.recv()
                    if (
                        event.event_type
                        == zlink.ServiceMonitorMask.SPOT_FILTER_APPLIED
                    ):
                        break

                warmup_deadline = time.perf_counter() + args.warmup
                while time.perf_counter() < warmup_deadline:
                    spot.publish(topic, [stamp_payload(payload)])

                active = True
                done.clear()
                started = time.perf_counter()
                deadline = started + args.duration
                while time.perf_counter() < deadline:
                    spot.publish(topic, [stamp_payload(payload)])

                active = False
                done.wait(1.0)
                elapsed = time.perf_counter() - started
                metrics = result_metrics(
                    count=active_count,
                    msg_size=args.msg_size,
                    elapsed_s=elapsed,
                    latencies_us=latencies,
                )
                print_result_lines("SPOT", "inproc", args.msg_size, metrics)
                sys.stdout.flush()
                os._exit(0)


if __name__ == "__main__":
    main()
