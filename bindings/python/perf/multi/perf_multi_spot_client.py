import socket
import os
import sys
import threading
import time

import zlink

from perf_multi_common import (
    TOPIC,
    attach_spot_service_pair,
    benchmark_run_id,
    latency_ns_from_message,
    is_active_message,
    parse_client_args,
    print_result_lines,
    resolve_multi_spot_control_settle_s,
    resolve_multi_spot_ready_settle_s,
    result_metrics,
)


SERVICE_NAME = "spot-svc"
READY_TIMEOUT_S = 15.0
START_TIMEOUT_S = 15.0
DRAIN_GRACE_S = 0.5


def _parse_tcp_endpoint(endpoint):
    host_port = endpoint.split("://", 1)[1]
    host, port_text = host_port.rsplit(":", 1)
    return host, int(port_text)


def _listen_tcp():
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind(("127.0.0.1", 0))
    sock.listen(1)
    return sock


def main(argv=None):
    args = parse_client_args(argv or sys.argv[1:], pattern="spot")
    if len(args.endpoint.split(",")) != 2:
        raise SystemExit("spot client expects --endpoint data_ep,control_ep")
    data_endpoint, control_endpoint = args.endpoint.split(",", 1)

    latencies = []
    received_count = 0
    run_id = benchmark_run_id()
    ready_settle_s = resolve_multi_spot_ready_settle_s()
    control_settle_s = resolve_multi_spot_control_settle_s()
    recv_lock = threading.Lock()
    runner_connected = threading.Event()
    runner_start = threading.Event()
    control_connected = threading.Event()
    started_event = threading.Event()
    started_size = [0]

    ready_listener = _listen_tcp()
    ready_host, ready_port = ready_listener.getsockname()
    print(f"CLIENT_CONTROL_ENDPOINT,tcp://{ready_host}:{ready_port}", flush=True)

    ready_sender = [None]

    def accept_ready_sender():
        conn, _addr = ready_listener.accept()
        ready_sender[0] = conn
        control_connected.set()

    threading.Thread(target=accept_ready_sender, daemon=True).start()

    start_reader = socket.create_connection(_parse_tcp_endpoint(control_endpoint), timeout=READY_TIMEOUT_S)
    start_file = start_reader.makefile("r", encoding="utf-8", newline="\n")

    def control_loop():
        for line in start_file:
            text = line.strip()
            if text.startswith("START,"):
                try:
                    started_size[0] = int(text.split(",", 1)[1])
                    started_event.set()
                except Exception:
                    pass
            elif text in {"STOP", "QUIT"}:
                return

    threading.Thread(target=control_loop, daemon=True).start()

    def stdin_loop():
        for line in sys.stdin:
            text = line.strip()
            if not text:
                continue
            if text.startswith("CONTROL_CONNECTED,"):
                runner_connected.set()
            elif text == f"START,{args.msg_size}":
                runner_start.set()
            elif text in {"STOP", "QUIT"}:
                return

    threading.Thread(target=stdin_loop, daemon=True).start()

    with zlink.Context() as ctx:
        clients = []
        service_pairs = []
        for index in range(args.clients):
            node = zlink.SpotNode(ctx)
            node.set_routing_id(f"SPOT-CLIENT-{index}".encode("ascii"))
            service_pairs.append(attach_spot_service_pair(ctx, node, SERVICE_NAME))
            node.connect_peer(data_endpoint)
            spot = node.create_spot()
            spot.set_subscription(TOPIC)
            clients.append((node, spot))

        def on_dispatch(spot, event):
            nonlocal received_count
            if event != zlink.SpotDispatchEvent.SUBSCRIBE_READABLE:
                return
            while True:
                try:
                    received = spot.subscribe(flags=zlink.RecvFlags.DONT_WAIT)
                except zlink.RecvError as exc:
                    if exc.result == zlink.RecvResult.NO_DATA:
                        return
                    raise
                with received:
                    parts = received.to_bytes_list()
                if not parts:
                    continue
                data = parts[0]
                if not is_active_message(
                    data,
                    expected_msg_size=args.msg_size,
                    run_id=run_id,
                ):
                    continue
                with recv_lock:
                    latencies.append(latency_ns_from_message(data))
                    received_count += 1

        for _, spot in clients:
            spot.on_dispatch_event(on_dispatch)

        deadline = time.perf_counter() + READY_TIMEOUT_S
        while time.perf_counter() < deadline:
            if control_connected.is_set() and runner_connected.is_set():
                break
            time.sleep(0.01)
        if not (control_connected.is_set() and runner_connected.is_set()):
            raise RuntimeError("control connection handshake timeout")

        time.sleep(ready_settle_s)
        time.sleep(control_settle_s)
        ready_sender[0].sendall(b"CONNECTED\n")
        ready_sender[0].sendall(f"READY_COUNT,{args.msg_size},{args.clients}\n".encode("utf-8"))
        print(f"CLIENT_READY,{args.msg_size}", flush=True)

        start_deadline = time.perf_counter() + START_TIMEOUT_S
        while time.perf_counter() < start_deadline:
            if runner_start.is_set() and started_event.is_set() and started_size[0] == args.msg_size:
                break
            time.sleep(0.01)
        if not (runner_start.is_set() and started_event.is_set() and started_size[0] == args.msg_size):
            raise RuntimeError("spot start handshake timeout")

        active_deadline = time.perf_counter() + args.duration
        while time.perf_counter() < active_deadline:
            time.sleep(0.01)
        drain_deadline = time.perf_counter() + DRAIN_GRACE_S
        while time.perf_counter() < drain_deadline:
            time.sleep(0.01)

        with recv_lock:
            if received_count <= 0:
                raise RuntimeError(
                    "multi spot benchmark did not receive any active message"
                )
            metrics = result_metrics(
                count=received_count,
                msg_size=args.msg_size,
                elapsed_s=max(args.duration, 0.001),
                latencies_ns=list(latencies),
            )
        print_result_lines("MULTI_SPOT", args.transport, args.msg_size, metrics)
        sys.stdout.flush()

        for node, spot in reversed(clients):
            try:
                spot.close()
            except Exception:
                pass
            try:
                node.close()
            except Exception:
                pass
        for pub_sock, sub_sock in reversed(service_pairs):
            try:
                sub_sock.close()
            except Exception:
                pass
            try:
                pub_sock.close()
            except Exception:
                pass
        os._exit(0)


if __name__ == "__main__":
    main()
