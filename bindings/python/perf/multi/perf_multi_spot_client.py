import os
import sys
import threading
import time

import zlink

from perf_multi_common import (
    TOPIC,
    apply_multi_spot_node_admission,
    bind_spot_node_endpoint,
    benchmark_run_id,
    configure_multi_tls_client,
    configure_multi_tls_server,
    decode_header,
    is_active_message,
    latency_ns_from_message,
    parse_client_args,
    perf_client_context,
    print_result_lines,
    publish_control_payload,
    receive_control_payload,
    resolve_multi_connect_ready_timeout_ms,
    resolve_multi_spot_control_settle_s,
    resolve_multi_spot_ready_settle_s,
    result_metrics,
)


def _trace(message):
    if os.environ.get("PERF_MULTI_SPOT_TRACE") == "1":
        print(f"[multi-spot-client] {message}", file=sys.stderr, flush=True)


def main(argv=None):
    args = parse_client_args(argv or sys.argv[1:], pattern="spot")
    if not args.control_endpoint:
        raise SystemExit("spot client expects --endpoint and --control-endpoint")

    run_id = benchmark_run_id()
    handshake_timeout_s = resolve_multi_connect_ready_timeout_ms() / 1000.0
    ready_settle_s = resolve_multi_spot_ready_settle_s()
    control_settle_s = resolve_multi_spot_control_settle_s()
    runner_start = threading.Event()
    control_connected = threading.Event()

    def stdin_loop():
        for line in sys.stdin:
            text = line.strip()
            if not text:
                continue
            if text == f"START,{args.msg_size}":
                runner_start.set()
            elif text.startswith("CONTROL_CONNECTED,"):
                control_connected.set()
            elif text in {"STOP", "QUIT"}:
                return

    threading.Thread(target=stdin_loop, daemon=True).start()

    with perf_client_context() as ctx:
        data_node = zlink.SpotNode(ctx)
        control_node = zlink.SpotNode(ctx)
        configure_multi_tls_client(data_node, args.transport)
        configure_multi_tls_server(control_node, args.transport)
        configure_multi_tls_client(control_node, args.transport)
        data_node.set_routing_id(b"a-python-multi-spot-client")
        control_node.set_routing_id(b"a-python-multi-spot-control-client")
        apply_multi_spot_node_admission(data_node, control_node)

        control_pub = control_node.create_spot()
        control_sub = control_node.create_spot()
        control_pub.set_routing_id(b"a-python-multi-spot-control-client-pub")
        control_sub.set_routing_id(b"a-python-multi-spot-control-client-sub")
        control_sub.set_subscription(TOPIC)
        control_endpoint = bind_spot_node_endpoint(
            control_node, args.transport, "multi-spot-control-client"
        )
        control_node.connect_peer(args.control_endpoint)
        print(f"CLIENT_CONTROL_ENDPOINT,{control_endpoint}", flush=True)

        spots = []
        data_node.connect_peer(args.endpoint)
        for index in range(args.clients):
            spot = data_node.create_spot()
            spot.set_routing_id(
                f"a-python-multi-spot-client-spot-{index}".encode("utf-8")
            )
            spot.set_subscription(TOPIC)
            spots.append(spot)

        if not control_connected.wait(timeout=handshake_timeout_s):
            raise RuntimeError("runner control-connected handshake timeout")

        time.sleep(ready_settle_s)
        time.sleep(control_settle_s)
        if not publish_control_payload(
            control_pub,
            f"READY_COUNT,{args.msg_size},{args.clients}",
            timeout_s=handshake_timeout_s,
        ):
            raise RuntimeError("spot control ready publish timeout")
        print(f"CLIENT_READY,{args.msg_size}", flush=True)

        if not runner_start.wait(timeout=handshake_timeout_s):
            raise RuntimeError("spot runner start handshake timeout")

        direct_start_deadline = time.perf_counter() + handshake_timeout_s
        direct_started = False
        while time.perf_counter() < direct_start_deadline:
            payload_text = receive_control_payload(control_sub)
            if payload_text is None:
                time.sleep(0.001)
                continue
            if payload_text == f"START,{args.msg_size}":
                direct_started = True
                break
        if not direct_started:
            raise RuntimeError("spot direct start handshake timeout")
        _trace("start-handshake-done")

        latencies = []
        received_count = 0
        active_deadline = time.perf_counter() + args.duration
        with zlink.Poller() as poller:
            for index, spot in enumerate(spots):
                poller.add_socket(spot, zlink.PollEventFlag.POLLIN, tag=index)
            while time.perf_counter() < active_deadline:
                events = poller.poll(10)
                if not events:
                    continue
                now = time.perf_counter()
                if now > active_deadline:
                    break
                for event in events:
                    current_spot = event.socket
                    while True:
                        message = zlink.TopicMessage()
                        try:
                            has_message = current_spot.subscribe_into(
                                message, flags=zlink.RecvFlags.DONT_WAIT
                            )
                        except zlink.RecvError as exc:
                            if exc.result == zlink.RecvResult.NO_DATA:
                                break
                            raise
                        if not has_message:
                            break
                        with message:
                            parts = message.to_bytes_list()
                        if not parts:
                            continue
                        data = parts[0]
                        header = decode_header(data)
                        if header is None:
                            continue
                        if (
                            header["run_id"] != run_id
                            or header["msg_size"] != args.msg_size
                            or header["phase"] == 0
                        ):
                            continue
                        if not is_active_message(
                            data,
                            expected_msg_size=args.msg_size,
                            run_id=run_id,
                        ):
                            continue
                        received_count += 1
                        latency = latency_ns_from_message(data)
                        if latency is not None:
                            latencies.append(latency)

        if received_count <= 0:
            raise RuntimeError("multi spot benchmark did not receive any active message")
        metrics = result_metrics(
            count=received_count,
            msg_size=args.msg_size,
            elapsed_s=max(args.duration, 0.001),
            latencies_ns=latencies,
        )
        print_result_lines("MULTI_SPOT", args.transport, args.msg_size, metrics)
        sys.stdout.flush()


if __name__ == "__main__":
    main()
