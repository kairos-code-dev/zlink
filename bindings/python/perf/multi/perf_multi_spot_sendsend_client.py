import sys
import threading
import time

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
    parse_client_args,
    perf_client_context,
    print_result_lines,
    publish_control_payload,
    receive_control_payload,
    resolve_multi_connect_ready_timeout_ms,
    resolve_multi_spot_control_settle_s,
    resolve_multi_spot_ready_settle_s,
    result_metrics,
    send_to_spot_nonblocking,
    stamp_payload,
)


SERVER_NODE_RID = b"SPOT-SENDSEND-SERVER-NODE"
SERVER_SPOT_RID = b"SPOT-SENDSEND-SERVER-SPOT"


def _active_slot_limit(total_slots, msg_size):
    if msg_size >= 131072:
        return min(total_slots, 8)
    if msg_size >= 65536:
        return min(total_slots, 32)
    return total_slots


def _drain_reply(spot, *, expected_msg_size, run_id, active_deadline, latencies, record):
    progressed = False
    received = zlink.Received()
    while True:
        try:
            has_received = spot.recv_routed_into(
                received, flags=zlink.RecvFlags.DONT_WAIT
            )
        except zlink.RecvError as exc:
            if exc.result == zlink.RecvResult.NO_DATA:
                return progressed
            raise
        if not has_received:
            return progressed
        progressed = True
        with received:
            if received.request_seq not in (None, 0):
                continue
            parts = received.to_bytes_list()
            if not parts:
                continue
            data = parts[-1]
            if (
                record
                and time.perf_counter() < active_deadline
                and is_active_message(
                    data, expected_msg_size=expected_msg_size, run_id=run_id
                )
            ):
                latency = latency_ns_from_message(data)
                if latency is not None:
                    latencies.append(latency / 2.0)


def main(argv=None):
    args = parse_client_args(argv or sys.argv[1:], pattern="spot_sendsend")
    if not args.control_endpoint:
        raise SystemExit("spot sendsend client expects --endpoint and --control-endpoint")

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
        data_node = zlink.SpotNode(ctx)
        control_node = zlink.SpotNode(ctx)
        configure_multi_tls_server(data_node, args.transport)
        configure_multi_tls_client(data_node, args.transport)
        configure_multi_tls_server(control_node, args.transport)
        configure_multi_tls_client(control_node, args.transport)
        apply_multi_spot_node_admission(data_node, control_node)
        data_node.set_routing_id(b"SPOT-SENDSEND-CLIENT-NODE")
        control_node.set_routing_id(b"SPOT-SENDSEND-CONTROL-CLIENT-NODE")

        control_pub = control_node.create_spot()
        control_sub = control_node.create_spot()
        control_pub.set_routing_id(b"SPOT-SENDSEND-CONTROL-CLIENT-PUB")
        control_sub.set_routing_id(b"SPOT-SENDSEND-CONTROL-CLIENT-SUB")
        control_sub.set_subscription(TOPIC)
        control_endpoint = bind_spot_node_endpoint(
            control_node, args.transport, "multi-spot-sendsend-control-client"
        )
        control_node.connect_peer(args.control_endpoint)
        print(f"CLIENT_CONTROL_ENDPOINT,{control_endpoint}", flush=True)

        data_node.set_router_bind_endpoint(
            benchmark_endpoint(args.transport, "multi-spot-sendsend-client-router")
        )
        data_endpoint_local = bind_spot_node_endpoint(
            data_node, args.transport, "multi-spot-sendsend-client"
        )
        data_node.connect_peer(args.endpoint)
        spots = []
        payloads = [bytearray(args.msg_size) for _ in range(args.clients)]
        seqs = [1 for _ in range(args.clients)]
        waiting = [False for _ in range(args.clients)]
        for index in range(args.clients):
            spot = data_node.create_spot()
            spot.set_routing_id(f"SPOT-SENDSEND-{index}".encode("ascii"))
            spots.append(spot)
        use_poll_wait = args.msg_size < 65536
        poller = None
        poll_events = None
        if use_poll_wait:
            poller = zlink.Poller()
            poll_events = zlink.PollEvents(max(1, len(spots)))
            for index, spot in enumerate(spots):
                poller.add_socket(spot, zlink.PollEventFlag.POLLIN, index)

        if not control_connected.wait(timeout=handshake_timeout_s):
            raise RuntimeError("runner control-connected handshake timeout")

        time.sleep(ready_settle_s)
        time.sleep(control_settle_s)
        if not publish_control_payload(
            control_pub,
            f"DATA_ENDPOINT,{data_endpoint_local}",
            timeout_s=handshake_timeout_s,
        ):
            raise RuntimeError("spot sendsend data endpoint publish timeout")
        time.sleep(control_settle_s)
        publish_control_payload(control_pub, "CONNECTED", timeout_s=handshake_timeout_s)

        probe_deadline = time.perf_counter() + handshake_timeout_s
        probe = stamp_payload(bytearray(args.msg_size), phase=0, run_id=run_id, seq=0)
        probe_waiting = False
        while time.perf_counter() < probe_deadline:
            if not probe_waiting and send_to_spot_nonblocking(
                spots[0], SERVER_NODE_RID, SERVER_SPOT_RID, probe
            ):
                probe_waiting = True
            if _drain_reply(
                spots[0],
                expected_msg_size=args.msg_size,
                run_id=run_id,
                active_deadline=probe_deadline,
                latencies=[],
                record=False,
            ):
                break
            if use_poll_wait:
                poller.wait(poll_events, 1)
            else:
                time.sleep(0.001)
        else:
            raise RuntimeError("spot sendsend probe-ready timeout")

        if not publish_control_payload(
            control_pub,
            f"READY_COUNT,{args.msg_size},{args.clients}",
            timeout_s=handshake_timeout_s,
        ):
            raise RuntimeError("spot sendsend ready publish timeout")
        print(f"CLIENT_READY,{args.msg_size}", flush=True)

        if not runner_start.wait(timeout=handshake_timeout_s):
            raise RuntimeError("spot sendsend runner start handshake timeout")

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
            raise RuntimeError("spot sendsend direct start handshake timeout")

        active_slots = _active_slot_limit(len(spots), args.msg_size)
        active_deadline = time.perf_counter() + args.duration
        while time.perf_counter() < active_deadline:
            progressed = False
            for index in range(active_slots):
                spot = spots[index]
                if waiting[index]:
                    if _drain_reply(
                        spot,
                        expected_msg_size=args.msg_size,
                        run_id=run_id,
                        active_deadline=active_deadline,
                        latencies=latencies,
                        record=True,
                    ):
                        waiting[index] = False
                        progressed = True
                    continue
                payload = stamp_payload(
                    payloads[index],
                    phase=1,
                    run_id=run_id,
                    seq=seqs[index],
                )
                if send_to_spot_nonblocking(
                    spot, SERVER_NODE_RID, SERVER_SPOT_RID, payload
                ):
                    waiting[index] = True
                    seqs[index] += 1
                    progressed = True
                else:
                    progressed |= _drain_reply(
                        spot,
                        expected_msg_size=args.msg_size,
                        run_id=run_id,
                        active_deadline=active_deadline,
                        latencies=latencies,
                        record=True,
                    )
            if not progressed:
                remaining_ms = int((active_deadline - time.perf_counter()) * 1000)
                if remaining_ms <= 0:
                    break
                if use_poll_wait:
                    poller.wait(poll_events, min(10, remaining_ms))
                else:
                    time.sleep(0.001)

        drain_deadline = time.perf_counter() + 1.0
        while any(waiting[:active_slots]) and time.perf_counter() < drain_deadline:
            progressed = False
            for index in range(active_slots):
                if not waiting[index]:
                    continue
                if _drain_reply(
                    spots[index],
                    expected_msg_size=args.msg_size,
                    run_id=run_id,
                    active_deadline=active_deadline,
                    latencies=latencies,
                    record=True,
                ):
                    waiting[index] = False
                    progressed = True
            if not progressed:
                if use_poll_wait:
                    poller.wait(poll_events, 20)
                else:
                    time.sleep(0.001)

        if not latencies:
            raise RuntimeError(
                "multi spot sendsend benchmark did not receive any active reply"
            )
        metrics = result_metrics(
            count=len(latencies),
            msg_size=args.msg_size,
            elapsed_s=max(args.duration, 0.001),
            latencies_ns=latencies,
            bandwidth_multiplier=2.0,
        )
        print_result_lines("MULTI_SPOT_SENDSEND", args.transport, args.msg_size, metrics)


if __name__ == "__main__":
    main()
