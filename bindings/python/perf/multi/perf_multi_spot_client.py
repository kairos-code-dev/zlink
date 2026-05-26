import os
import sys
import struct
import threading
import time

import zlink

from perf_multi_common import (
    HEADER_MAGIC,
    HEADER_SIZE,
    TOPIC,
    apply_multi_auto_hwm_msg_unit,
    apply_multi_spot_node_admission,
    bind_spot_node_endpoint,
    benchmark_run_id,
    configure_multi_tls_client,
    configure_multi_tls_server,
    parse_client_args,
    perf_client_context,
    print_result_lines,
    publish_control_payload,
    receive_control_payload,
    new_spot_poller,
    resolve_multi_connect_ready_timeout_ms,
    resolve_multi_spot_control_settle_s,
    resolve_multi_spot_ready_settle_s,
    result_metrics,
    wait_control_readable_until,
)
from perf_metrics import HEADER_FORMAT


def _trace(message):
    if os.environ.get("PERF_MULTI_SPOT_TRACE") == "1":
        print(f"[multi-spot-client] {message}", file=sys.stderr, flush=True)


def _close_all(*resources):
    for resource in resources:
        try:
            resource.close()
        except Exception:
            pass


def _latency_sample_stride():
    try:
        return max(1, int(os.environ.get("PERF_MULTI_SPOT_LATENCY_SAMPLE_STRIDE", "32")))
    except ValueError:
        return 32


def _subscribe_active_metric_once(spot, storage, *, msg_size, run_id, sample_latency):
    try:
        if not spot.subscribe_into(storage, flags=zlink.RecvFlags.DONT_WAIT):
            return None
    except zlink.RecvError as exc:
        if exc.result == zlink.RecvResult.NO_DATA:
            return None
        raise

    try:
        parts = storage.parts
        data = parts[0].to_bytes() if parts else b""
        size = len(data)
        if size != msg_size or size < HEADER_SIZE:
            return False, None
        magic, hdr_run_id, phase, hdr_msg_size, _seq, sent_ts_ns = struct.unpack_from(
            HEADER_FORMAT, data, 0
        )
        if (
            magic != HEADER_MAGIC
            or phase != 1
            or hdr_msg_size != msg_size
            or hdr_run_id != run_id
        ):
            return False, None
        if not sample_latency:
            return True, None
        now_ns = time.time_ns()
        if sent_ts_ns > 0 and now_ns >= sent_ts_ns:
            return True, float(now_ns - sent_ts_ns)
        return True, 0.0
    finally:
        storage.close()


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
        apply_multi_auto_hwm_msg_unit(ctx, args.msg_size)
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
        control_poller, control_events = new_spot_poller(
            control_sub, zlink.PollEventFlag.POLLIN
        )
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
                wait_control_readable_until(
                    control_poller, control_events, direct_start_deadline
                )
                continue
            if payload_text == f"START,{args.msg_size}":
                direct_started = True
                break
        if not direct_started:
            raise RuntimeError("spot direct start handshake timeout")
        _trace("start-handshake-done")

        latencies = []
        received_count = 0
        sample_stride = _latency_sample_stride()
        active_deadline = time.perf_counter() + args.duration
        _trace(f"active-start duration={args.duration} clients={len(spots)}")
        with zlink.Poller() as poller:
            poll_events = zlink.PollEvents(max(1, len(spots)))
            for index, spot in enumerate(spots):
                poller.add_socket(spot, zlink.PollEventFlag.POLLIN, index)
            while time.perf_counter() < active_deadline:
                remaining_ms = int((active_deadline - time.perf_counter()) * 1000)
                if remaining_ms <= 0:
                    break
                ready_count = poller.wait(poll_events, min(10, remaining_ms))
                if not ready_count:
                    continue
                now = time.perf_counter()
                if now > active_deadline:
                    break
                expired = False
                for offset in range(ready_count):
                    if time.perf_counter() >= active_deadline:
                        expired = True
                        break
                    index = poll_events.slot(offset)
                    if index < 0 or index >= len(spots):
                        continue
                    current_spot = spots[index]
                    storage = zlink.TopicMessage()
                    while True:
                        if time.perf_counter() >= active_deadline:
                            expired = True
                            break
                        try:
                            received_metric = _subscribe_active_metric_once(
                                current_spot,
                                storage,
                                msg_size=args.msg_size,
                                run_id=run_id,
                                sample_latency=(
                                    (received_count + 1) % sample_stride == 0
                                ),
                            )
                        except zlink.RecvError as exc:
                            if exc.result == zlink.RecvResult.NO_DATA:
                                break
                            raise
                        if received_metric is None:
                            break
                        is_active, latency = received_metric
                        if not is_active:
                            continue
                        received_count += 1
                        if latency is not None:
                            latencies.append(latency)
                    if expired:
                        break
                if expired:
                    break

        _trace(f"active-end received={received_count} latencies={len(latencies)}")
        if received_count <= 0:
            raise RuntimeError("multi spot benchmark did not receive any active message")
        control_poller.close()
        _close_all(*spots, control_sub, control_pub, control_node, data_node)
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
