import os
import sys
import time

import zlink

from perf_multi_common import (
    CallbackMetrics,
    TOPIC,
    parse_client_args,
    print_result_lines,
    result_metrics,
)


def _make_client(ctx, endpoint, index):
    node = zlink.SpotNode(ctx)
    node.set_routing_id(f"SPOT-CLIENT-{index}".encode("ascii"))
    node.connect_peer(endpoint)
    spot = node.wrap_handle()
    spot.set_subscription(TOPIC)
    return node, spot


def main(argv=None):
    args = parse_client_args(
        argv or sys.argv[1:], pattern="spot", allowed_recv={"callback"}
    )
    clients = []
    metrics_sink = CallbackMetrics()

    with zlink.Context() as ctx:
        try:
            for index in range(args.clients):
                node, spot = _make_client(ctx, args.endpoint, index)
                clients.append((node, spot))
            for _, spot in clients:
                spot.on_subscribe(
                    lambda message: metrics_sink.on_payload(
                        message.to_bytes_list()[0]
                    )
                )

            warmup_deadline = time.perf_counter() + args.warmup
            while time.perf_counter() < warmup_deadline:
                time.sleep(0.01)
            metrics_sink.activate()
            started = time.perf_counter()
            time.sleep(args.duration)
            metrics_sink.deactivate()

            if not metrics_sink.wait_ready(1.0):
                raise RuntimeError('spot client did not receive any message')
            count, latencies = metrics_sink.finish()
            elapsed = args.duration
            metrics = result_metrics(
                count=count,
                msg_size=args.msg_size,
                elapsed_s=max(elapsed, 0.001),
                latencies_us=latencies,
            )
            print_result_lines("MULTI_SPOT", "tcp", args.msg_size, metrics)
            sys.stdout.flush()
            os._exit(0)
        finally:
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
