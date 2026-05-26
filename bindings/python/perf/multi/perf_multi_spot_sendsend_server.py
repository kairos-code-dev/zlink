import os
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
    configure_multi_tls_client,
    configure_multi_tls_server,
    parse_server_args,
    perf_server_context,
    publish_control_payload,
    receive_control_payload,
    resolve_multi_spot_server_ready_timeout_s,
    wait_spot_peer_connected,
)


SERVER_NODE_RID = b"SPOT-SENDSEND-SERVER-NODE"
SERVER_SPOT_RID = b"SPOT-SENDSEND-SERVER-SPOT"


def _drain_replier(replier):
    received = zlink.Received()
    while True:
        try:
            has_received = replier.recv_routed_into(
                received, flags=zlink.RecvFlags.DONT_WAIT
            )
        except zlink.RecvError as exc:
            if exc.result == zlink.RecvResult.NO_DATA:
                return
            raise
        if not has_received:
            return
        with received:
            if received.request_seq not in (None, 0):
                continue
            try:
                (
                    received.send()
                    .messages(*received.to_bytes_list())
                    .flags(zlink.SendFlags.DONT_WAIT)
                    .submit()
                )
            except zlink.SubmitError as exc:
                if exc.result != zlink.SubmitResult.BACKPRESSURED:
                    raise


def main(argv=None):
    args = parse_server_args(argv or sys.argv[1:])
    handshake_timeout_s = resolve_multi_spot_server_ready_timeout_s()
    stop = threading.Event()
    start_runner = threading.Event()
    data_connected = threading.Event()

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
        data_node = zlink.SpotNode(ctx)
        control_node = zlink.SpotNode(ctx)
        configure_multi_tls_server(data_node, args.transport)
        configure_multi_tls_server(control_node, args.transport)
        configure_multi_tls_client(control_node, args.transport)
        apply_multi_spot_node_admission(data_node, control_node)
        data_node.set_routing_id(SERVER_NODE_RID)
        control_node.set_routing_id(b"SPOT-SENDSEND-CONTROL-SERVER-NODE")

        replier = data_node.create_spot()
        control_pub = control_node.create_spot()
        control_sub = control_node.create_spot()
        replier.set_routing_id(SERVER_SPOT_RID)
        control_pub.set_routing_id(b"SPOT-SENDSEND-CONTROL-SERVER-PUB")
        control_sub.set_routing_id(b"SPOT-SENDSEND-CONTROL-SERVER-SUB")
        control_sub.set_subscription(TOPIC)

        data_node.set_router_bind(
            benchmark_endpoint(args.transport, "multi-spot-sendsend-router")
        )
        data_endpoint = bind_spot_node_endpoint(
            data_node, args.transport, "multi-spot-sendsend"
        )
        control_endpoint = bind_spot_node_endpoint(
            control_node, args.transport, "multi-spot-sendsend-control-server"
        )
        print(f"READY,{data_endpoint}", flush=True)
        print(f"CONTROL_READY,{control_endpoint}", flush=True)

        threading.Thread(target=stdin_loop, args=(control_node,), daemon=True).start()
        poller = zlink.Poller()
        poll_events = zlink.PollEvents(1)
        poller.add_socket(replier, zlink.PollEventFlag.POLLIN, 0)

        ready_units = 0
        deadline = time.perf_counter() + handshake_timeout_s
        while time.perf_counter() < deadline and not stop.is_set():
            _drain_replier(replier)
            payload_text = receive_control_payload(control_sub)
            if payload_text is None:
                poller.wait(poll_events, 1)
                continue
            if payload_text.startswith("DATA_ENDPOINT,"):
                endpoint = payload_text.split(",", 1)[1]
                data_node.connect_peer(endpoint)
                data_connected.set()
            elif payload_text == "CONNECTED":
                continue
            elif payload_text.startswith("READY_COUNT,"):
                try:
                    _cmd, size_text, count_text = payload_text.split(",", 2)
                    if int(size_text) == args.msg_size:
                        ready_units += int(count_text)
                except ValueError:
                    continue
            if data_connected.is_set() and ready_units >= args.clients:
                break
        if not data_connected.is_set() or ready_units < args.clients:
            raise RuntimeError("spot sendsend server readiness timeout")

        start_deadline = time.perf_counter() + handshake_timeout_s
        while time.perf_counter() < start_deadline and not stop.is_set():
            _drain_replier(replier)
            if start_runner.wait(0):
                break
            poller.wait(poll_events, 1)
        if not start_runner.is_set():
            raise RuntimeError("spot sendsend server start handshake timeout")

        if not publish_control_payload(
            control_pub, f"START,{args.msg_size}", timeout_s=handshake_timeout_s
        ):
            raise RuntimeError("spot sendsend control start publish timeout")

        idle_seconds = max(
            1.0,
            float(os.environ.get("PERF_MULTI_DURATION_SECONDS", str(args.duration))),
        ) + float(os.environ.get("PERF_MULTI_SPOT_SERVER_IDLE_S", "2.0"))
        idle_deadline = time.perf_counter() + idle_seconds
        while not stop.is_set() and time.perf_counter() < idle_deadline:
            _drain_replier(replier)
            if stop.wait(0):
                break
            poller.wait(poll_events, 1)


if __name__ == "__main__":
    main()
