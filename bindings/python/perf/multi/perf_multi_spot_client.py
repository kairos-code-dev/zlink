import os
import sys
import time

import zlink

from perf_multi_common import (
    CallbackMetrics,
    TOPIC,
    latency_us_from_message,
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


def _wait_spot_client_ready(spot, timeout_s):
    with spot.open_monitor(
        zlink.ServiceMonitorMask.SPOT_FILTER_APPLIED
        | zlink.ServiceMonitorMask.PEER_UP
    ) as monitor:
        deadline = time.perf_counter() + timeout_s
        filter_ready = False
        peer_ready = False
        while time.perf_counter() < deadline:
            event = monitor.recv()
            if event.event_type == zlink.ServiceMonitorMask.SPOT_FILTER_APPLIED:
                filter_ready = True
            elif event.event_type == zlink.ServiceMonitorMask.PEER_UP:
                peer_ready = True
            if filter_ready and peer_ready:
                return
    raise RuntimeError('timed out waiting for spot client readiness')


def main(argv=None):
    args = parse_client_args(
        argv or sys.argv[1:], pattern="spot", allowed_recv={"recv", "callback"}
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
            elapsed = args.duration
            metrics = result_metrics(
                count=metrics_sink.count,
                msg_size=args.msg_size,
                elapsed_s=max(elapsed, 0.001),
                latencies_us=metrics_sink.latencies,
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
