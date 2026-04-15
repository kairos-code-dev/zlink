import sys
import time
import uuid

import zlink

from perf_common import (
    attach_spot_service_pair,
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
TOPIC = b"bench"


def main(argv=None):
    args = parse_single_args(argv or sys.argv[1:], pattern="spot")
    payload = new_payload(args.msg_size)
    topic = f"bench.{uuid.uuid4().hex}".encode("ascii")
    run_id = benchmark_run_id()
    latencies = []
    with zlink.Context() as ctx:
        with zlink.SpotNode(ctx) as node:
            pub_sock, sub_sock = attach_spot_service_pair(ctx, node, SERVICE_NAME)
            try:
                with node.create_spot() as spot:
                    spot.set_subscription(topic)
                    spot.publish(SERVICE_NAME, topic, [stamp_payload(payload, phase=0)])
                    warmup_deadline = time.monotonic() + 2.0
                    while time.monotonic() < warmup_deadline:
                        received = recv_nonblocking(spot, method="subscribe")
                        if received is None:
                            continue
                        with received:
                            data = received.to_bytes_list()[0]
                            if is_active_message(
                                data,
                                expected_msg_size=args.msg_size,
                                run_id=run_id,
                            ):
                                latencies.append(latency_ns_from_message(data))
                                break

                    spot.publish(SERVICE_NAME, topic, [stamp_payload(payload, phase=1)])
                    active_deadline = time.monotonic() + 2.0
                    while time.monotonic() < active_deadline:
                        received = recv_nonblocking(spot, method="subscribe")
                        if received is None:
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
                            break

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
                    print_result_lines("SPOT", "inproc", args.msg_size, metrics)
            finally:
                sub_sock.close()
                pub_sock.close()


if __name__ == "__main__":
    main()
