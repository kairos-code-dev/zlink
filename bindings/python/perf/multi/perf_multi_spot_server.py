import os
import sys
import threading
import time

import zlink

from perf_multi_common import (
    TOPIC,
    apply_multi_auto_hwm_msg_unit,
    apply_multi_spot_node_admission,
    bind_spot_node_endpoint,
    benchmark_run_id,
    configure_multi_tls_client,
    configure_multi_tls_server,
    new_payload,
    parse_server_args,
    perf_server_context,
    publish_control_payload,
    receive_control_payload,
    resolve_multi_spot_server_ready_timeout_s,
    new_spot_poller,
    spot_publish_nonblocking,
    stamp_payload,
    wait_control_readable_until,
    wait_spot_peer_connected,
    wait_spot_writable_until,
)


def _trace(message):
    if os.environ.get("PERF_MULTI_SPOT_TRACE") == "1":
        print(f"[multi-spot-server] {message}", file=sys.stderr, flush=True)


def _close_all(*resources):
    for resource in resources:
        try:
            resource.close()
        except Exception:
            pass


def main(argv=None):
    args = parse_server_args(argv or sys.argv[1:])
    run_id = benchmark_run_id()
    payload = new_payload(args.msg_size)
    active_duration_s = max(
        1.0,
        float(os.environ.get("PERF_MULTI_DURATION_SECONDS", str(args.duration))),
    )
    handshake_timeout_s = resolve_multi_spot_server_ready_timeout_s()
    stop = threading.Event()
    start_runner = threading.Event()

    def stdin_loop(control_node):
        for line in sys.stdin:
            text = line.strip()
            if not text:
                continue
            if text.startswith("CONNECT_CONTROL,"):
                endpoint = text.split(",", 1)[1]
                control_node.connect_peer(endpoint)
                wait_spot_peer_connected(control_node, handshake_timeout_s)
                print(f"CONTROL_CONNECTED,{endpoint}", flush=True)
            elif text == f"START,{args.msg_size}":
                start_runner.set()
            elif text in {"STOP", "QUIT"}:
                stop.set()
                return

    with perf_server_context() as ctx:
        apply_multi_auto_hwm_msg_unit(ctx, args.msg_size)
        data_node = zlink.create_spot_node(ctx)
        control_node = zlink.create_spot_node(ctx)
        configure_multi_tls_server(data_node, args.transport)
        configure_multi_tls_server(control_node, args.transport)
        configure_multi_tls_client(control_node, args.transport)
        data_node.set_routing_id(b"z-python-multi-spot-server")
        control_node.set_routing_id(b"z-python-multi-spot-control-server")
        apply_multi_spot_node_admission(data_node, control_node)

        data_spot = data_node.create_spot()
        control_pub = control_node.create_spot()
        control_sub = control_node.create_spot()
        data_spot.set_routing_id(b"z-python-multi-spot-server-spot")
        control_pub.set_routing_id(b"z-python-multi-spot-control-server-pub")
        control_sub.set_routing_id(b"z-python-multi-spot-control-server-sub")
        control_sub.set_subscription(TOPIC)
        control_poller, control_events = new_spot_poller(
            control_sub, zlink.PollEventFlag.POLLIN
        )

        data_endpoint = bind_spot_node_endpoint(data_node, args.transport, "multi-spot-data")
        control_endpoint = bind_spot_node_endpoint(
            control_node, args.transport, "multi-spot-control-server"
        )
        print(f"READY,{data_endpoint}", flush=True)
        print(f"CONTROL_READY,{control_endpoint}", flush=True)

        threading.Thread(target=stdin_loop, args=(control_node,), daemon=True).start()

        ready_units = 0
        ready_deadline = time.perf_counter() + handshake_timeout_s
        while time.perf_counter() < ready_deadline and not stop.is_set():
            payload_text = receive_control_payload(control_sub)
            if payload_text is None:
                wait_control_readable_until(
                    control_poller, control_events, ready_deadline
                )
                continue
            if payload_text.startswith("READY_COUNT,"):
                try:
                    _cmd, size_text, count_text = payload_text.split(",", 2)
                    if int(size_text) == args.msg_size:
                        ready_units += int(count_text)
                        _trace(f"ready-count units={ready_units}")
                except ValueError:
                    continue
            if ready_units >= args.clients:
                break
        if ready_units < args.clients:
            raise RuntimeError("spot control readiness timeout")

        start_deadline = time.perf_counter() + handshake_timeout_s
        while time.perf_counter() < start_deadline and not stop.is_set():
            if start_runner.wait(0.01):
                break
        if not start_runner.is_set():
            raise RuntimeError("spot server start handshake timeout")

        if not publish_control_payload(
            control_pub, f"START,{args.msg_size}", timeout_s=handshake_timeout_s
        ):
            raise RuntimeError("spot control start publish timeout")

        active_deadline = time.perf_counter() + active_duration_s
        data_write_poller, data_write_events = new_spot_poller(
            data_spot, zlink.PollEventFlag.POLLOUT
        )
        while not stop.is_set() and time.perf_counter() < active_deadline:
            sent = spot_publish_nonblocking(
                data_spot,
                "spot-svc",
                TOPIC,
                [stamp_payload(payload, phase=1, run_id=run_id)],
            )
            if not sent:
                wait_spot_writable_until(
                    data_write_poller, data_write_events, active_deadline
                )
        control_poller.close()
        data_write_poller.close()
        _close_all(control_sub, control_pub, data_spot, control_node, data_node)
        sys.stdout.flush()


if __name__ == "__main__":
    main()
