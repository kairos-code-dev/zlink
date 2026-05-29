import os
import sys
import threading
import time
from queue import SimpleQueue

import zlink

from perf_multi_common import (
    TOPIC,
    apply_multi_auto_hwm_msg_unit,
    apply_multi_spot_node_admission,
    benchmark_endpoint,
    bind_spot_node_endpoint,
    benchmark_run_id,
    configure_multi_tls_client,
    configure_multi_tls_server,
    is_active_message,
    latency_ns_from_message,
    metric_payload_data,
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
    stamp_payload,
    wait_control_readable_until,
    wait_spot_peer_connected,
)


SERVER_NODE_RID = b"SPOT-REQREP-SERVER-NODE"
SERVER_SPOT_RID = b"SPOT-REQREP-SERVER-SPOT"


def _active_slot_limit(total_slots, msg_size):
    if msg_size >= 131072:
        return min(total_slots, 8)
    if msg_size >= 65536:
        return min(total_slots, 32)
    return total_slots


def main(argv=None):
    args = parse_client_args(argv or sys.argv[1:], pattern="spot_reqrep")
    if not args.control_endpoint:
        raise SystemExit("spot reqrep client expects --endpoint and --control-endpoint")

    run_id = benchmark_run_id()
    latencies = []
    ready_settle_s = resolve_multi_spot_ready_settle_s()
    control_settle_s = resolve_multi_spot_control_settle_s()
    handshake_timeout_s = resolve_multi_connect_ready_timeout_ms() / 1000.0
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
        data_node = zlink.create_spot_node(ctx)
        control_node = zlink.create_spot_node(ctx)
        configure_multi_tls_server(data_node, args.transport)
        configure_multi_tls_client(data_node, args.transport)
        configure_multi_tls_server(control_node, args.transport)
        configure_multi_tls_client(control_node, args.transport)
        apply_multi_spot_node_admission(data_node, control_node)
        data_node.set_routing_id(b"SPOT-REQREP-CLIENT-NODE")
        control_node.set_routing_id(b"SPOT-REQREP-CONTROL-CLIENT-NODE")

        control_pub = control_node.create_spot()
        control_sub = control_node.create_spot()
        control_pub.set_routing_id(b"SPOT-REQREP-CONTROL-CLIENT-PUB")
        control_sub.set_routing_id(b"SPOT-REQREP-CONTROL-CLIENT-SUB")
        control_sub.set_subscription(TOPIC)
        control_endpoint = bind_spot_node_endpoint(
            control_node, args.transport, "multi-spot-reqrep-control-client"
        )
        control_node.connect_peer(args.control_endpoint)
        print(f"CLIENT_CONTROL_ENDPOINT,{control_endpoint}", flush=True)

        data_node.set_router_bind(
            benchmark_endpoint(args.transport, "multi-spot-reqrep-client-router")
        )
        data_endpoint_local = bind_spot_node_endpoint(
            data_node, args.transport, "multi-spot-reqrep-client"
        )
        data_node.connect_peer(args.endpoint)
        spots = []
        payloads = [bytearray(args.msg_size) for _ in range(args.clients)]
        seq = 0
        for index in range(args.clients):
            spot = data_node.create_spot()
            spot.set_routing_id(f"spot-req-client-spot-{index}".encode("ascii"))
            spots.append(spot)
        poller = zlink.create_poller()
        poll_events = zlink.create_poll_events(max(1, len(spots)))
        for index, spot in enumerate(spots):
            poller.add_socket(spot, zlink.PollEventFlag.POLLCOMPLETION, index)

        if not control_connected.wait(timeout=handshake_timeout_s):
            raise RuntimeError("runner control-connected handshake timeout")

        time.sleep(ready_settle_s)
        time.sleep(control_settle_s)
        if not publish_control_payload(
            control_pub,
            f"DATA_ENDPOINT,{data_endpoint_local}",
            timeout_s=handshake_timeout_s,
        ):
            raise RuntimeError("spot reqrep data endpoint publish timeout")
        time.sleep(control_settle_s)
        if not wait_spot_peer_connected(data_node, handshake_timeout_s):
            raise RuntimeError("spot reqrep data peer connection timeout")
        publish_control_payload(control_pub, "CONNECTED", timeout_s=handshake_timeout_s)
        time.sleep(control_settle_s)

        if not publish_control_payload(
            control_pub,
            f"READY_COUNT,{args.msg_size},{args.clients}",
            timeout_s=handshake_timeout_s,
        ):
            raise RuntimeError("spot reqrep ready publish timeout")
        print(f"CLIENT_READY,{args.msg_size}", flush=True)

        if not runner_start.wait(timeout=handshake_timeout_s):
            raise RuntimeError("spot reqrep runner start handshake timeout")

        direct_start_deadline = time.perf_counter() + handshake_timeout_s
        control_poller, control_events = new_spot_poller(
            control_sub, zlink.PollEventFlag.POLLIN
        )
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
            raise RuntimeError("spot reqrep direct start handshake timeout")
        control_poller.close()

        active_deadline = time.perf_counter() + args.duration
        waiting = [False] * len(spots)
        lock = threading.Lock()
        latency_queue = SimpleQueue()
        active_slots = _active_slot_limit(len(spots), args.msg_size)
        while time.perf_counter() < active_deadline:
            submitted_any = False
            for index in range(active_slots):
                spot = spots[index]
                with lock:
                    if waiting[index]:
                        continue
                    waiting[index] = True
                seq += 1
                payload = stamp_payload(
                    payloads[index],
                    phase=1,
                    run_id=run_id,
                    seq=seq,
                )

                def on_reply(result, messages, slot_index=index):
                    try:
                        if result != zlink.RequestResult.OK or not messages:
                            return
                        data = metric_payload_data(messages[0].data)
                        if (
                            time.perf_counter() < active_deadline
                            and is_active_message(
                                data,
                                expected_msg_size=args.msg_size,
                                run_id=run_id,
                            )
                        ):
                            latency = latency_ns_from_message(data)
                            if latency is not None:
                                latency_queue.put(latency / 2.0)
                    finally:
                        with lock:
                            waiting[slot_index] = False

                submitted = (
                    False
                )
                try:
                    submitted = (
                        spot.request_to_spot(SERVER_NODE_RID, SERVER_SPOT_RID)
                        .message(bytes(payload))
                        .timeout(0.2)
                        .flags(zlink.SendFlags.DONT_WAIT)
                        .submit(on_reply)
                    )
                except zlink.SubmitError as exc:
                    if exc.result not in (
                        zlink.SubmitResult.BACKPRESSURED,
                        zlink.SubmitResult.NOT_CONNECTED,
                    ):
                        raise
                if submitted:
                    submitted_any = True
                else:
                    with lock:
                        waiting[index] = False
            while not latency_queue.empty():
                latencies.append(latency_queue.get())
            if submitted_any:
                continue
            remaining_ms = int((active_deadline - time.perf_counter()) * 1000)
            if remaining_ms <= 0:
                break
            poller.wait(poll_events, min(50, remaining_ms))

        drain_deadline = time.perf_counter() + 1.0
        while any(waiting[:active_slots]) and time.perf_counter() < drain_deadline:
            poller.wait(poll_events, 50)
            while not latency_queue.empty():
                latencies.append(latency_queue.get())

        while not latency_queue.empty():
            latencies.append(latency_queue.get())

        if not latencies:
            raise RuntimeError(
                "multi spot reqrep benchmark did not receive any active reply"
            )
        metrics = result_metrics(
            count=len(latencies),
            msg_size=args.msg_size,
            elapsed_s=max(args.duration, 0.001),
            latencies_ns=latencies,
            bandwidth_multiplier=2.0,
        )
        print_result_lines("MULTI_SPOT_REQREP", args.transport, args.msg_size, metrics)


if __name__ == "__main__":
    main()
    sys.stdout.flush()
    sys.stderr.flush()
    os._exit(0)
