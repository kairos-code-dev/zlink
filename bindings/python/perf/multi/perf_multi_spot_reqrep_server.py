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
    configure_multi_tls_client,
    configure_multi_tls_server,
    parse_server_args,
    perf_server_context,
    publish_control_payload,
    receive_control_payload,
    resolve_multi_connect_ready_timeout_ms,
)


SERVER_NODE_RID = b"SPOT-REQREP-SERVER-NODE"
SERVER_SPOT_RID = b"SPOT-REQREP-SERVER-SPOT"


def main(argv=None):
    args = parse_server_args(argv or sys.argv[1:])
    handshake_timeout_s = resolve_multi_connect_ready_timeout_ms() / 1000.0
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
        control_node.set_routing_id(b"SPOT-REQREP-CONTROL-SERVER-NODE")

        replier = data_node.create_spot()
        control_pub = control_node.create_spot()
        control_sub = control_node.create_spot()
        replier.set_routing_id(SERVER_SPOT_RID)
        control_pub.set_routing_id(b"SPOT-REQREP-CONTROL-SERVER-PUB")
        control_sub.set_routing_id(b"SPOT-REQREP-CONTROL-SERVER-SUB")
        control_sub.set_subscription(TOPIC)

        def on_dispatch(current_spot, info):
            if info.event != zlink.SpotDispatchEvent.ROUTED_READABLE:
                return
            received = zlink.Received()
            while True:
                try:
                    has_received = current_spot.recv_routed_into(
                        received, flags=zlink.RecvFlags.DONT_WAIT
                    )
                except zlink.RecvError as exc:
                    if exc.result == zlink.RecvResult.NO_DATA:
                        return
                    raise
                if not has_received:
                    return
                with received:
                    received.reply().messages(*received.to_bytes_list()).submit()

        replier.on_dispatch_event(on_dispatch)

        data_endpoint = bind_spot_node_endpoint(
            data_node, args.transport, "multi-spot-reqrep"
        )
        control_endpoint = bind_spot_node_endpoint(
            control_node, args.transport, "multi-spot-reqrep-control-server"
        )
        print(f"READY,{data_endpoint}", flush=True)
        print(f"CONTROL_READY,{control_endpoint}", flush=True)

        threading.Thread(target=stdin_loop, args=(control_node,), daemon=True).start()

        ready_units = 0
        deadline = time.perf_counter() + handshake_timeout_s
        while time.perf_counter() < deadline and not stop.is_set():
            payload_text = receive_control_payload(control_sub)
            if payload_text is None:
                time.sleep(0.001)
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
            raise RuntimeError("spot reqrep server readiness timeout")

        start_deadline = time.perf_counter() + handshake_timeout_s
        while time.perf_counter() < start_deadline and not stop.is_set():
            if start_runner.wait(0.01):
                break
        if not start_runner.is_set():
            raise RuntimeError("spot reqrep server start handshake timeout")

        if not publish_control_payload(
            control_pub, f"START,{args.msg_size}", timeout_s=handshake_timeout_s
        ):
            raise RuntimeError("spot reqrep control start publish timeout")

        idle_seconds = max(
            1.0,
            float(os.environ.get("PERF_MULTI_DURATION_SECONDS", str(args.duration))),
        ) + float(os.environ.get("PERF_MULTI_SPOT_SERVER_IDLE_S", "2.0"))
        stop.wait(idle_seconds)


if __name__ == "__main__":
    main()
