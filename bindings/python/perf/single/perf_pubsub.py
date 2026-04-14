import sys
import threading
import time

import zlink

from perf_common import (
    benchmark_run_id,
    latency_ns_from_message,
    is_active_message,
    new_payload,
    parse_single_args,
    print_result_lines,
    result_metrics,
    recv_nonblocking,
    stamp_payload,
    tcp_endpoint,
)


TOPIC = b"bench"


def main(argv=None):
    args = parse_single_args(argv or sys.argv[1:], pattern="pubsub")
    payload = new_payload(args.msg_size)
    run_id = benchmark_run_id()
    latencies = []

    def send_loop(publisher):
        warmup_end = time.perf_counter() + args.warmup
        while time.perf_counter() < warmup_end:
            publisher.publish(TOPIC, stamp_payload(payload, phase=0))

        active_end = time.perf_counter() + args.duration
        while time.perf_counter() < active_end:
            publisher.publish(TOPIC, stamp_payload(payload, phase=1))
    with zlink.Context() as ctx:
        with zlink.PubSocket(ctx) as publisher:
            with zlink.SubSocket(ctx) as subscriber:
                endpoint = tcp_endpoint("pubsub")
                publisher.bind(endpoint)
                subscriber.set_subscription(TOPIC)
                subscriber.connect(endpoint)
                time.sleep(1.0)

                sender = threading.Thread(
                    target=send_loop, args=(publisher,), daemon=True
                )
                sender.start()
                drain_deadline = time.perf_counter() + args.duration + 1.0

                while True:
                    received = recv_nonblocking(subscriber, method="subscribe")
                    if received is None:
                        if time.perf_counter() >= drain_deadline:
                            break
                        time.sleep(0.001)
                        continue
                    with received:
                        data = received.to_bytes_list()[0]
                        if not is_active_message(
                            data,
                            expected_msg_size=args.msg_size,
                            run_id=run_id,
                        ):
                            continue
                        latencies.append(latency_ns_from_message(data))

                sender.join()
                if not latencies:
                    raise RuntimeError("pubsub benchmark did not receive any active message")
                metrics = result_metrics(
                    count=len(latencies),
                    msg_size=args.msg_size,
                    elapsed_s=args.duration,
                    latencies_ns=latencies,
                )
                print_result_lines("PUBSUB", "tcp", args.msg_size, metrics)


if __name__ == "__main__":
    main()
