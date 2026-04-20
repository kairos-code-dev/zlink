import os
import socket
import sys
import threading
import time

import zlink

from perf_multi_common import (
    TOPIC,
    apply_multi_socket_options,
    attach_spot_service_pair,
    benchmark_endpoint,
    benchmark_run_id,
    new_payload,
    parse_server_args,
    safe_poll,
    spot_publish_nonblocking,
    stamp_payload,
)


SERVICE_NAME = "spot-svc"
CONNECT_TIMEOUT_S = 15.0


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
    args = parse_server_args(argv or sys.argv[1:])
    run_id = benchmark_run_id()
    payload = new_payload(args.msg_size)
    active_duration_s = max(
        1.0,
        float(os.environ.get("PERF_MULTI_DURATION_SECONDS", "5")),
    )
    stop = threading.Event()
    start_runner = threading.Event()
    control_connected = threading.Event()
    ready_count = [0]
    ready_lock = threading.Lock()
    start_sender = [None]
    ready_reader = [None]
    control_peer_endpoint = [""]

    start_listener = _listen_tcp()
    start_host, start_port = start_listener.getsockname()

    def accept_start_reader():
        conn, _addr = start_listener.accept()
        start_sender[0] = conn

    threading.Thread(target=accept_start_reader, daemon=True).start()

    def stdin_loop():
        for line in sys.stdin:
            text = line.strip()
            if not text:
                continue
            if text.startswith("CONNECT_CONTROL,"):
                endpoint = text.split(",", 1)[1]
                control_peer_endpoint[0] = endpoint
                ready_reader[0] = socket.create_connection(
                    _parse_tcp_endpoint(endpoint),
                    timeout=CONNECT_TIMEOUT_S,
                )
                control_connected.set()
                print(f"CONTROL_CONNECTED,{endpoint}", flush=True)
            elif text == f"START,{args.msg_size}":
                start_runner.set()
            elif text in {"STOP", "QUIT"}:
                stop.set()
                return

    def recv_control():
        deadline = time.perf_counter() + CONNECT_TIMEOUT_S
        while time.perf_counter() < deadline and ready_reader[0] is None and not stop.is_set():
            time.sleep(0.01)
        if ready_reader[0] is None:
            return
        fileobj = ready_reader[0].makefile("r", encoding="utf-8", newline="\n")
        for line in fileobj:
            text = line.strip()
            if text == "CONNECTED":
                control_connected.set()
            elif text.startswith("READY_COUNT,"):
                _, size_text, count_text = text.split(",", 2)
                if int(size_text) == args.msg_size:
                    with ready_lock:
                        ready_count[0] += int(count_text)
            elif text in {"STOP", "QUIT"}:
                return

    threading.Thread(target=stdin_loop, daemon=True).start()
    threading.Thread(target=recv_control, daemon=True).start()

    with zlink.Context() as ctx:
        data_node = zlink.SpotNode(ctx)
        service_pair = attach_spot_service_pair(ctx, data_node, SERVICE_NAME)
        apply_multi_socket_options(service_pair[0])
        data_node.bind(benchmark_endpoint(args.transport, "multi-spot"))
        data_endpoint = data_node.last_endpoint()
        data_spot = data_node.create_spot()

        print(f"READY,{data_endpoint}", flush=True)
        print(f"CONTROL_READY,tcp://{start_host}:{start_port}", flush=True)

        deadline = time.perf_counter() + CONNECT_TIMEOUT_S
        while time.perf_counter() < deadline:
            with ready_lock:
                ready_ok = ready_count[0] >= args.clients
            if control_connected.is_set() and start_runner.is_set() and ready_ok and start_sender[0] is not None:
                break
            if stop.is_set():
                return
            time.sleep(0.01)
        with ready_lock:
            ready_ok = ready_count[0] >= args.clients
        if not (control_connected.is_set() and start_runner.is_set() and ready_ok and start_sender[0] is not None):
            raise RuntimeError("spot server handshake timeout")

        start_sender[0].sendall(f"START,{args.msg_size}\n".encode("utf-8"))
        active_deadline = time.perf_counter() + active_duration_s
        with zlink.Poller() as poller:
            poller.add_socket(service_pair[0], zlink.PollEvent.POLLOUT)
            while not stop.is_set():
                if time.perf_counter() >= active_deadline:
                    stop.wait(0.01)
                    continue
                events = safe_poll(poller, 100)
                if not events:
                    continue
                for event in events:
                    if not (event["events"] & int(zlink.PollEvent.POLLOUT)):
                        continue
                    while (
                        time.perf_counter() < active_deadline
                        and not stop.is_set()
                    ):
                        sent = spot_publish_nonblocking(
                            data_spot,
                            SERVICE_NAME,
                            TOPIC,
                            [stamp_payload(payload, phase=1, run_id=run_id)],
                        )
                        if not sent:
                            break
        sys.stdout.flush()
        try:
            data_spot.close()
        except Exception:
            pass
        try:
            data_node.close()
        except Exception:
            pass
        try:
            service_pair[1].close()
        except Exception:
            pass
        try:
            service_pair[0].close()
        except Exception:
            pass


if __name__ == "__main__":
    main()
