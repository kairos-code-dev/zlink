import sys
import threading
import time
import uuid

import zlink

from perf_common import (
    benchmark_run_id,
    latency_ns_from_message,
    is_active_message,
    new_payload,
    parse_single_args,
    print_result_lines,
    result_metrics,
    safe_poll,
    recv_nonblocking,
    stamp_payload,
)


def main(argv=None):
    args = parse_single_args(argv or sys.argv[1:], pattern="spot")
    payload = new_payload(args.msg_size)
    topic = f"bench.{uuid.uuid4().hex}".encode("ascii")
    run_id = benchmark_run_id()
    latencies = []
    ready_event = threading.Event()

    def send_loop(spot):
        while not ready_event.is_set():
            spot.publish(topic, [stamp_payload(payload, phase=0)])
            ready_event.wait(0.001)

        warmup_end = time.perf_counter() + args.warmup
        while time.perf_counter() < warmup_end:
            spot.publish(topic, [stamp_payload(payload, phase=0)])

        active_end = time.perf_counter() + args.duration
        while time.perf_counter() < active_end:
            spot.publish(topic, [stamp_payload(payload, phase=1)])
    with zlink.Context() as ctx:
        with zlink.SpotNode(ctx) as pub_node:
            with zlink.SpotNode(ctx) as sub_node:
                with zlink.Spot(pub_node) as pub_spot:
                    with zlink.Spot(sub_node) as sub_spot:
                        endpoint = "inproc://py-perf-spot"
                        pub_node.bind(endpoint)
                        sub_node.connect_peer(endpoint)
                        sub_spot.set_subscription(topic)

                        sender = threading.Thread(target=send_loop, args=(pub_spot,), daemon=True)
                        sender.start()
                        drain_deadline = time.perf_counter() + args.duration + 1.0

                        with zlink.Poller() as poller:
                            poller.add_socket(sub_spot, zlink.PollEvent.POLLIN)
                            while True:
                                safe_poll(poller, 50)
                                while True:
                                    received = recv_nonblocking(
                                        sub_spot, method="subscribe"
                                    )
                                    if received is None:
                                        break
                                    with received:
                                        data = received.to_bytes_list()[0]
                                        if not ready_event.is_set():
                                            ready_event.set()
                                        if not is_active_message(
                                            data,
                                            expected_msg_size=args.msg_size,
                                            run_id=run_id,
                                        ):
                                            continue
                                        latencies.append(latency_ns_from_message(data))
                                if time.perf_counter() >= drain_deadline:
                                    break

                        sender.join()
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


if __name__ == "__main__":
    main()
