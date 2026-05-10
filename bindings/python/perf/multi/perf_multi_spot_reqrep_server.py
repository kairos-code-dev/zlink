import os
import sys
import threading
import time
import socket

import zlink

from perf_multi_common import (
    apply_multi_spot_node_admission,
    benchmark_endpoint,
    parse_server_args,
    resolve_multi_connect_ready_timeout_ms,
)


CHANNEL_NAME = "spot-svc"
SERVER_NODE_RID = b"SPOT-REQREP-SERVER-NODE"
SERVER_SPOT_RID = b"SPOT-REQREP-SERVER-SPOT"


def main(argv=None):
    args = parse_server_args(argv or sys.argv[1:])
    connect_timeout_s = resolve_multi_connect_ready_timeout_ms() / 1000.0
    stop = threading.Event()
    start_runner = threading.Event()
    control_connected = threading.Event()
    data_connected = threading.Event()
    ready_count = [0]
    ready_lock = threading.Lock()
    control_peer_endpoint = [""]
    ready_reader = [None]

    with zlink.Context() as ctx:
        with zlink.SpotNode(ctx) as data_node:
            apply_multi_spot_node_admission(data_node)
            data_node.set_routing_id(SERVER_NODE_RID)
            data_node.bind(benchmark_endpoint(args.transport, "multi-spot-reqrep"))
            data_endpoint = data_node.last_endpoint()
            with data_node.create_spot() as replier:
                replier.set_routing_id(SERVER_SPOT_RID)
                control_listener = None
                start_sender = [None]

                def accept_start_reader():
                    conn, _addr = control_listener.accept()
                    start_sender[0] = conn

                control_listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                control_listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
                control_listener.bind(("127.0.0.1", 0))
                control_listener.listen(1)
                start_host, start_port = control_listener.getsockname()
                threading.Thread(target=accept_start_reader, daemon=True).start()

                def stdin_loop():
                    for line in sys.stdin:
                        text = line.strip()
                        if not text:
                            continue
                        if text.startswith("CONNECT_CONTROL,"):
                            endpoint = text.split(",", 1)[1]
                            control_peer_endpoint[0] = endpoint
                            host_port = endpoint.split("://", 1)[1]
                            host, port_text = host_port.rsplit(":", 1)
                            ready_reader[0] = socket.create_connection(
                                (host, int(port_text)),
                                timeout=connect_timeout_s,
                            )
                            control_connected.set()
                        elif text == f"START,{args.msg_size}":
                            start_runner.set()
                        elif text in {"STOP", "QUIT"}:
                            stop.set()
                            return

                def recv_control():
                    deadline = time.perf_counter() + connect_timeout_s
                    while (
                        time.perf_counter() < deadline
                        and ready_reader[0] is None
                        and not stop.is_set()
                    ):
                        stop.wait(0.01)
                    if ready_reader[0] is None:
                        return
                    fileobj = ready_reader[0].makefile(
                        "r", encoding="utf-8", newline="\n"
                    )
                    for line in fileobj:
                        text = line.strip()
                        if text == "CONNECTED":
                            control_connected.set()
                        elif text.startswith("DATA_ENDPOINT,"):
                            endpoint = text.split(",", 1)[1]
                            data_node.connect_peer(endpoint)
                            data_connected.set()
                        elif text.startswith("READY_COUNT,"):
                            _, size_text, count_text = text.split(",", 2)
                            if int(size_text) == args.msg_size:
                                with ready_lock:
                                    ready_count[0] += int(count_text)
                        elif text in {"STOP", "QUIT"}:
                            return

                def on_dispatch(current_spot, info):
                    if info.event != zlink.SpotDispatchEvent.ROUTED_READABLE:
                        return
                    while True:
                        try:
                            received = current_spot.recv_routed(
                                flags=zlink.RecvFlags.DONT_WAIT
                            )
                        except zlink.RecvError as exc:
                            if exc.result == zlink.RecvResult.NO_DATA:
                                return
                            raise
                        with received:
                            received.reply(received.to_bytes_list())

                replier.on_dispatch_event(on_dispatch)

                threading.Thread(target=stdin_loop, daemon=True).start()
                threading.Thread(target=recv_control, daemon=True).start()

                print(f"READY,{data_endpoint}", flush=True)
                print(f"CONTROL_READY,tcp://127.0.0.1:{start_port}", flush=True)

                deadline = time.perf_counter() + connect_timeout_s
                while time.perf_counter() < deadline:
                    with ready_lock:
                        ready_ok = ready_count[0] >= args.clients
                    if (
                        control_connected.is_set()
                        and data_connected.is_set()
                        and start_runner.is_set()
                        and ready_ok
                        and start_sender[0] is not None
                    ):
                        break
                    if stop.is_set():
                        return
                    stop.wait(0.01)
                with ready_lock:
                    ready_ok = ready_count[0] >= args.clients
                if not (
                    control_connected.is_set()
                    and data_connected.is_set()
                    and start_runner.is_set()
                    and ready_ok
                    and start_sender[0] is not None
                ):
                    raise RuntimeError("spot reqrep server handshake timeout")

                start_sender[0].sendall(f"START,{args.msg_size}\n".encode("utf-8"))
                idle_seconds = max(
                    1.0,
                    float(os.environ.get("PERF_MULTI_DURATION_SECONDS", "5")),
                ) + float(os.environ.get("PERF_MULTI_SPOT_SERVER_IDLE_S", "2.0"))
                # PERF_MULTI_TEST_POLICY § 1.3.1: single blocking wait; the
                # dispatch callback handles request/reply asynchronously and
                # the runner ends this server via stdin STOP/EOF or SIGTERM.
                stop.wait(idle_seconds)


if __name__ == "__main__":
    main()
