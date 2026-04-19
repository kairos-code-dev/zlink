import sys
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
)


SERVICE_NAME = "spot-svc"
TOPIC = b"bench.topic"


def main(argv=None):
    args = parse_single_args(argv or sys.argv[1:], pattern="spot")
    run_id = benchmark_run_id()
    latencies = []
    with zlink.Context() as ctx:
        node = zlink.SpotNode(ctx)
        try:
            with zlink.Discovery(ctx, zlink.ServiceType.SPOT, SERVICE_NAME) as discovery:
                node.attach_discovery(discovery)
                with node.create_spot() as spot:
                    spot.set_subscription(TOPIC)

                    warmup_end = time.monotonic() + args.warmup
                    while time.monotonic() < warmup_end:
                        spot.publish(
                            SERVICE_NAME,
                            TOPIC,
                            [
                                stamp_payload(
                                    new_payload(args.msg_size),
                                    phase=0,
                                    run_id=run_id,
                                )
                            ],
                        )
                        while True:
                            received = recv_nonblocking(spot, method="subscribe")
                            if received is None:
                                break
                            with received:
                                pass

                    active_end = time.monotonic() + args.duration
                    while time.monotonic() < active_end:
                        spot.publish(
                            SERVICE_NAME,
                            TOPIC,
                            [
                                stamp_payload(
                                    new_payload(args.msg_size),
                                    phase=1,
                                    run_id=run_id,
                                )
                            ],
                        )
                        while True:
                            received = recv_nonblocking(spot, method="subscribe")
                            if received is None:
                                break
                            with received:
                                data = received.to_bytes_list()[0]
                                if not is_active_message(
                                    data,
                                    expected_msg_size=args.msg_size,
                                    run_id=run_id,
                                ):
                                    continue
                                latencies.append(latency_ns_from_message(data))

                    if not latencies:
                        raise RuntimeError(
                            "spot benchmark did not receive any active message"
                        )
                    metrics = result_metrics(
                        count=len(latencies),
                        msg_size=args.msg_size,
                        elapsed_s=args.duration,
                        latencies_ns=latencies,
                    )
                    print_result_lines("SPOT", args.transport, args.msg_size, metrics)
        finally:
            pass


if __name__ == "__main__":
    main()
