import sys
import threading
import time

import zlink

from perf_common import (
    HEADER_MAGIC,
    benchmark_run_id,
    decode_header,
    latency_ns_from_message,
    is_active_message,
    new_payload,
    parse_single_args,
    print_result_lines,
    recv_nonblocking,
    resolve_single_endpoint,
    resolve_single_recv_timeout_ms,
    resolve_single_spot_ready_settle_s,
    result_metrics,
    stamp_payload,
)


SERVICE_NAME = "spot-svc"
TOPIC = b"bench.topic"
READY_TIMEOUT_S = 5.0


def main(argv=None):
    args = parse_single_args(argv or sys.argv[1:], pattern="spot")
    run_id = benchmark_run_id()
    latencies = []
    probe_ready = threading.Event()
    recv_lock = threading.Lock()
    probe_payload = new_payload(args.msg_size)
    active_payload = new_payload(args.msg_size)
    cooldown_payload = new_payload(args.msg_size)

    with zlink.Context() as ctx:
        with zlink.SpotNode(ctx) as publisher_node:
            with zlink.SpotNode(ctx) as subscriber_node:
                with publisher_node.create_spot() as publisher:
                    with subscriber_node.create_spot() as subscriber:
                        endpoint = resolve_single_endpoint(args.transport, "spot")
                        publisher_node.bind(endpoint)
                        subscriber.set_subscription(TOPIC)
                        subscriber_node.connect_peer(endpoint)

                        def on_dispatch(current_spot, event):
                            if event != zlink.SpotDispatchEvent.SUBSCRIBE_READABLE:
                                return
                            while True:
                                received = recv_nonblocking(
                                    current_spot, method="subscribe"
                                )
                                if received is None:
                                    return
                                with received:
                                    parts = received.to_bytes_list()
                                if not parts:
                                    continue
                                data = parts[0]
                                header = decode_header(data)
                                if (
                                    not probe_ready.is_set()
                                    and header is not None
                                    and header["magic"] == HEADER_MAGIC
                                    and header["phase"] == 0
                                    and header["msg_size"] == args.msg_size
                                    and header["run_id"] == run_id
                                ):
                                    probe_ready.set()
                                    continue
                                if not is_active_message(
                                    data,
                                    expected_msg_size=args.msg_size,
                                    run_id=run_id,
                                ):
                                    continue
                                with recv_lock:
                                    latencies.append(latency_ns_from_message(data))

                        subscriber.on_dispatch_event(on_dispatch)

                        ready_deadline = time.monotonic() + READY_TIMEOUT_S
                        while (
                            not probe_ready.is_set()
                            and time.monotonic() < ready_deadline
                        ):
                            publisher.publish(
                                SERVICE_NAME,
                                TOPIC,
                                [
                                    stamp_payload(
                                        probe_payload,
                                        phase=0,
                                        run_id=run_id,
                                    )
                                ],
                            )
                            probe_ready.wait(0.05)
                        if not probe_ready.is_set():
                            raise RuntimeError("spot benchmark probe-ready timeout")

                        time.sleep(resolve_single_spot_ready_settle_s())

                        active_end = time.monotonic() + args.duration
                        while time.monotonic() < active_end:
                            publisher.publish(
                                SERVICE_NAME,
                                TOPIC,
                                [
                                    stamp_payload(
                                        active_payload,
                                        phase=1,
                                        run_id=run_id,
                                    )
                                ],
                            )

                        publisher.publish(
                            SERVICE_NAME,
                            TOPIC,
                            [
                                stamp_payload(
                                    cooldown_payload,
                                    phase=2,
                                    run_id=run_id,
                                )
                            ],
                        )
                        time.sleep(resolve_single_recv_timeout_ms() / 1000.0)

                        with recv_lock:
                            collected = list(latencies)
                        if not collected:
                            raise RuntimeError(
                                "spot benchmark did not receive any active message"
                            )
                        metrics = result_metrics(
                            count=len(collected),
                            msg_size=args.msg_size,
                            elapsed_s=args.duration,
                            latencies_ns=collected,
                        )
                        print_result_lines(
                            "SPOT", args.transport, args.msg_size, metrics
                        )


if __name__ == "__main__":
    main()
