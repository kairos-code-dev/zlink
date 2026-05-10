import os
import socket
import sys
import threading
import time

import zlink

from perf_multi_common import (
    STOP_TOKEN,
    TOPIC,
    apply_multi_spot_node_admission,
    benchmark_endpoint,
    benchmark_run_id,
    configure_multi_tls_server,
    new_payload,
    parse_server_args,
    safe_poll,
    spot_publish_nonblocking,
    stamp_payload,
    resolve_multi_connect_ready_timeout_ms,
)


CHANNEL_NAME = "spot-svc"


def _trace(message):
    if os.environ.get("PERF_MULTI_SPOT_TRACE") == "1":
        print(f"[multi-spot-server] {message}", file=sys.stderr, flush=True)
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
    connect_timeout_s = resolve_multi_connect_ready_timeout_ms() / 1000.0
    stop = threading.Event()
    start_runner = threading.Event()
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
                    timeout=connect_timeout_s,
                )
                ready_reader[0].settimeout(None)
            elif text == f"START,{args.msg_size}":
                start_runner.set()
            elif text in {"STOP", "QUIT"}:
                stop.set()
                return

    def recv_control():
        deadline = time.perf_counter() + connect_timeout_s
        while time.perf_counter() < deadline and ready_reader[0] is None and not stop.is_set():
            stop.wait(0.01)
        if ready_reader[0] is None:
            return
        fileobj = ready_reader[0].makefile("r", encoding="utf-8", newline="\n")
        for line in fileobj:
            text = line.strip()
            if text == "CONNECTED":
                continue
            elif text.startswith("READY_COUNT,"):
                _, size_text, count_text = text.split(",", 2)
                if int(size_text) == args.msg_size:
                    with ready_lock:
                        ready_count[0] = int(count_text)
            elif text in {"STOP", "QUIT"}:
                return

    threading.Thread(target=stdin_loop, daemon=True).start()
    threading.Thread(target=recv_control, daemon=True).start()

    with zlink.Context() as ctx:
        registry = zlink.Registry(ctx)
        discovery = zlink.Discovery(ctx, zlink.AutoConnectType.SPOT_MESH, CHANNEL_NAME)
        data_node = zlink.SpotNode(ctx)
        configure_multi_tls_server(data_node, args.transport)
        data_node.set_routing_id(b"z-python-multi-spot-server")
        data_spot = data_node.create_spot()
        data_spot.set_routing_id(b"z-python-multi-spot-server-spot")
        registry_pub_endpoint = benchmark_endpoint(args.transport, "multi-spot-registry-pub")
        registry_router_endpoint = benchmark_endpoint(args.transport, "multi-spot-registry-router")
        data_bind_endpoint = benchmark_endpoint(args.transport, "multi-spot-data")
        registry.bind(registry_pub_endpoint, registry_router_endpoint)
        registry.set_broadcast_interval(50)
        discovery.connect_registry(registry_router_endpoint)
        apply_multi_spot_node_admission(data_node)
        data_node.bind(data_bind_endpoint)
        data_node.attach_discovery(discovery)

        print(f"READY,{registry_router_endpoint}", flush=True)
        print(f"CONTROL_READY,tcp://{start_host}:{start_port}", flush=True)

        deadline = time.perf_counter() + connect_timeout_s
        while time.perf_counter() < deadline:
            if start_sender[0] is not None and ready_reader[0] is not None:
                break
            if stop.is_set():
                return
            stop.wait(0.01)
        if start_sender[0] is None or ready_reader[0] is None:
            raise RuntimeError("spot server control handshake timeout")
        _trace("control-handshake-ready")

        ready_deadline = time.perf_counter() + connect_timeout_s
        ready_ok = False
        while time.perf_counter() < ready_deadline and not stop.is_set():
            with ready_lock:
                ready_ok = ready_count[0] >= args.clients
            if ready_ok:
                break
            time.sleep(0.001)
        if not ready_ok:
            raise RuntimeError("spot control readiness timeout")
        _trace(f"control-ready count={ready_count[0]}")

        runner_deadline = time.perf_counter() + connect_timeout_s
        while time.perf_counter() < runner_deadline:
            if start_runner.is_set():
                break
            if stop.is_set():
                return
            stop.wait(0.01)
        if not start_runner.is_set():
            raise RuntimeError("spot server start handshake timeout")

        start_sender[0].sendall(f"START,{args.msg_size}\n".encode("utf-8"))
        active_deadline = time.perf_counter() + active_duration_s
        idle_deadline = active_deadline + float(
            os.environ.get("PERF_MULTI_SPOT_SERVER_IDLE_S", "2.0")
        )
        # PERF_MULTI_TEST_POLICY § 1.3.1: publish phase=1 until deadline,
        # then keep emitting wire stop tokens periodically for the cooldown
        # window. Spot publish is broadcast to all subscribed peers on the
        # mesh, so a single token reaches every client; we publish multiple
        # to absorb any HWM drops.
        while not stop.is_set() and time.perf_counter() < active_deadline:
            sent = spot_publish_nonblocking(
                data_spot,
                CHANNEL_NAME,
                TOPIC,
                [stamp_payload(payload, phase=1, run_id=run_id)],
            )
            if not sent:
                # Producer backpressure backoff (not shutdown sync).
                time.sleep(0.001)
        while not stop.is_set() and time.perf_counter() < idle_deadline:
            spot_publish_nonblocking(
                data_spot,
                CHANNEL_NAME,
                TOPIC,
                [STOP_TOKEN],
            )
            time.sleep(0.001)
        sys.stdout.flush()
        try:
            data_spot.close()
        except Exception as exc:
            print(f"[perf] close failed: {exc}", file=sys.stderr)
        try:
            data_node.close()
        except Exception as exc:
            print(f"[perf] close failed: {exc}", file=sys.stderr)
        try:
            discovery.close()
        except Exception as exc:
            print(f"[perf] close failed: {exc}", file=sys.stderr)
        try:
            registry.close()
        except Exception as exc:
            print(f"[perf] close failed: {exc}", file=sys.stderr)


if __name__ == "__main__":
    main()
