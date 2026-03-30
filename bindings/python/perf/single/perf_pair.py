import os
import sys
import threading
import time

import zlink

from perf_common import (
    latency_us_from_message,
    new_payload,
    parse_single_args,
    print_result_lines,
    result_metrics,
    stamp_payload,
    tcp_endpoint,
)


def main(argv=None):
    args = parse_single_args(argv or sys.argv[1:], pattern="pair")
    payload = new_payload(args.msg_size)
    latencies = []
    active = False
    active_count = 0
    drain = threading.Event()

    with zlink.Context() as ctx:
        with zlink.PairSocket(ctx) as server:
            with zlink.PairSocket(ctx) as client:
                endpoint = tcp_endpoint("pair")

                def on_message(received):
                    nonlocal active_count
                    part = received.to_bytes_list()[0]
                    if active:
                        latencies.append(latency_us_from_message(part))
                        active_count += 1
                        drain.set()

                server.on_receive(on_message)
                server.bind(endpoint)
                client.connect(endpoint)

                warmup_deadline = time.perf_counter() + args.warmup
                while time.perf_counter() < warmup_deadline:
                    client.send(stamp_payload(payload))

                active = True
                drain.clear()
                started = time.perf_counter()
                deadline = started + args.duration
                while time.perf_counter() < deadline:
                    client.send(stamp_payload(payload))

                active = False
                drain.wait(1.0)
                elapsed = time.perf_counter() - started
                metrics = result_metrics(
                    count=active_count,
                    msg_size=args.msg_size,
                    elapsed_s=elapsed,
                    latencies_us=latencies,
                )
                print_result_lines("PAIR", "tcp", args.msg_size, metrics)
                sys.stdout.flush()
                os._exit(0)


if __name__ == "__main__":
    main()
