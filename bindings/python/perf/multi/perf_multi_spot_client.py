import sys
import threading
import time

import zlink

from perf_multi_common import (
    TOPIC,
    latency_us_from_message,
    parse_client_args,
    print_result_lines,
    result_metrics,
)


def _make_client(ctx, endpoint, index):
    node = zlink.SpotNode(ctx)
    try:
        node.set_routing_id(f"SPOT-CLIENT-{index}".encode("ascii"))
        node.connect_peer(endpoint)
        spot = node.wrap_handle()
        try:
            spot.set_subscription(TOPIC)
        except Exception:
            spot.close()
            raise
    except Exception:
        node.close()
        raise
    return node, spot


def main(argv=None):
    args = parse_client_args(
        argv or sys.argv[1:], pattern="spot", allowed_recv={"recv", "callback"}
    )
    latencies = []
    count = 0
    lock = threading.Lock()
    ready = threading.Event()
    stop = threading.Event()
    active = threading.Event()
    clients = []

    with zlink.Context() as ctx:
        try:
            for index in range(args.clients):
                clients.append(_make_client(ctx, args.endpoint, index))

            def record_message(message):
                nonlocal count
                sample = latency_us_from_message(message.to_bytes_list()[0])
                with lock:
                    ready.set()
                    if active.is_set():
                        count += 1
                        latencies.append(sample)

            if args.recv == "callback":
                for _, spot in clients:
                    spot.set_handler(record_message)

            warmup_deadline = time.perf_counter() + args.warmup
            deadline = warmup_deadline + args.duration
            while time.perf_counter() < deadline and not stop.is_set():
                if not active.is_set() and time.perf_counter() >= warmup_deadline:
                    active.set()
                for _, spot in clients:
                    if args.recv == "recv":
                        message = spot.try_recv()
                        if message is None:
                            continue
                        with message:
                            ready.set()
                            if active.is_set():
                                count += 1
                                latencies.append(
                                    latency_us_from_message(
                                        message.to_bytes_list()[0]
                                    )
                                )
                time.sleep(0.0005)

            if not ready.is_set():
                raise RuntimeError("spot client did not receive any message")
            metrics = result_metrics(
                count=count,
                msg_size=args.msg_size,
                elapsed_s=max(args.duration, 0.001),
                latencies_us=latencies,
            )
            print_result_lines("MULTI_SPOT", "tcp", args.msg_size, metrics)
        finally:
            stop.set()
            for node, spot in reversed(clients):
                try:
                    spot.close()
                except Exception:
                    pass
                try:
                    node.close()
                except Exception:
                    pass


if __name__ == "__main__":
    main()
