import sys
import time

import zlink

from perf_common import (
    latency_us_from_message,
    new_payload,
    parse_single_args,
    print_result_lines,
    result_metrics,
    safe_poll,
    stamp_payload,
    unique_endpoint,
    wait_pubsub_ready,
)


TOPIC = b"bench"


def main(argv=None):
    args = parse_single_args(argv or sys.argv[1:], pattern="pubsub")
    payload = new_payload(args.msg_size)
    latencies = []
    count = 0

    with zlink.Context() as ctx:
        with zlink.PubSocket(ctx) as publisher:
            with zlink.SubSocket(ctx) as subscriber:
                endpoint = unique_endpoint("pubsub")
                publisher.bind(endpoint)
                subscriber.connect(endpoint)
                subscriber.set_subscription(TOPIC)
                wait_pubsub_ready(publisher, subscriber)

                with zlink.Poller() as poller:
                    poller.add_socket(subscriber, zlink.PollEvent.POLLIN)

                    warmup_deadline = time.perf_counter() + args.warmup
                    while time.perf_counter() < warmup_deadline:
                        publisher.publish(TOPIC, stamp_payload(payload))
                        for event in safe_poll(poller, 0):
                            while True:
                                received = event["socket"].try_subscribe()
                                if received is None:
                                    break
                                with received:
                                    pass

                    started = time.perf_counter()
                    deadline = started + args.duration
                    while time.perf_counter() < deadline:
                        publisher.publish(TOPIC, stamp_payload(payload))
                        for event in safe_poll(poller, 0):
                            while True:
                                received = event["socket"].try_subscribe()
                                if received is None:
                                    break
                                with received:
                                    latencies.append(
                                        latency_us_from_message(received.to_bytes_list()[0])
                                    )
                                    count += 1

                if count == 0:
                    raise RuntimeError("pubsub benchmark did not receive any message")
                elapsed = time.perf_counter() - started
                metrics = result_metrics(
                    count=count,
                    msg_size=args.msg_size,
                    elapsed_s=elapsed,
                    latencies_us=latencies,
                )
                print_result_lines("PUBSUB", "inproc", args.msg_size, metrics)


if __name__ == "__main__":
    main()
