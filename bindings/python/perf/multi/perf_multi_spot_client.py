import os
import socket
import sys
import threading
import time
from functools import partial

import zlink

from perf_multi_common import (
    TOPIC,
    apply_multi_spot_node_admission,
    benchmark_endpoint,
    benchmark_run_id,
    configure_multi_tls_client,
    decode_header,
    latency_ns_from_message,
    is_active_message,
    parse_client_args,
    print_result_lines,
    resolve_multi_connect_ready_timeout_ms,
    resolve_multi_spot_control_settle_s,
    resolve_multi_spot_ready_settle_s,
    result_metrics,
)


SERVICE_NAME = "spot-svc"
WAIT_SLICE_S = 0.01
IDLE_DRAIN_S = 2.0


def _trace(message):
    if os.environ.get("PERF_MULTI_SPOT_TRACE") == "1":
        print(f"[multi-spot-client] {message}", file=sys.stderr, flush=True)


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
        raise SystemExit("spot client expects --endpoint registry_router_ep,control_ep")
    registry_router_endpoint, control_endpoint = args.endpoint.split(",", 1)

    latencies = []
    received_count = 0
    run_id = benchmark_run_id()
    ready_settle_s = resolve_multi_spot_ready_settle_s()
    control_settle_s = resolve_multi_spot_control_settle_s()
    ready_timeout_s = resolve_multi_connect_ready_timeout_ms() / 1000.0
    recv_lock = threading.Lock()
    runner_start = threading.Event()
    control_connected = threading.Event()
    started_event = threading.Event()
    started_size = [0]
    cooldown_ready = threading.Event()
    cooldown_seen = set()
    collect_active = [False]
    active_deadline = [0.0]

    ready_listener = _listen_tcp()
    ready_host, ready_port = ready_listener.getsockname()
    print(f"CLIENT_CONTROL_ENDPOINT,tcp://{ready_host}:{ready_port}", flush=True)

    ready_sender = [None]

    def accept_ready_sender():
        conn, _addr = ready_listener.accept()
        ready_sender[0] = conn
        control_connected.set()

    threading.Thread(target=accept_ready_sender, daemon=True).start()

    start_reader = socket.create_connection(
        _parse_tcp_endpoint(control_endpoint), timeout=ready_timeout_s
    )
    start_reader.settimeout(None)
    start_file = start_reader.makefile("r", encoding="utf-8", newline="\n")
    print(f"CONTROL_CONNECTED,{control_endpoint}", flush=True)

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
            if text == f"START,{args.msg_size}":
                runner_start.set()
            elif text in {"STOP", "QUIT"}:
                return

    threading.Thread(target=stdin_loop, daemon=True).start()

    with zlink.Context() as ctx:
        clients = []
        node = zlink.SpotNode(ctx)
        configure_multi_tls_client(node, args.transport)
        discovery = zlink.Discovery(ctx, zlink.AutoConnectType.SPOT_MESH, SERVICE_NAME)
        node.set_routing_id(b"a-python-multi-spot-client")
        discovery.connect_registry(registry_router_endpoint)
        apply_multi_spot_node_admission(node)
        node.bind(benchmark_endpoint(args.transport, "multi-spot-client"))
        node.attach_discovery(discovery)

        def on_dispatch(index, current_spot, info):
            if info.event != zlink.SpotDispatchEvent.SUBSCRIBE_READABLE:
                return
            while True:
                received = None
                try:
                    received = current_spot.subscribe(flags=zlink.RecvFlags.DONT_WAIT)
                except zlink.RecvError as exc:
                    if exc.result == zlink.RecvResult.NO_DATA:
                        return
                    raise
                if received is None:
                    return
                with received:
                    parts = received.to_bytes_list()
                if not parts:
                    continue
                data = parts[0]
                if not data:
                    continue
                header = decode_header(data)
                if header is None:
                    continue
                if header["run_id"] != run_id or header["msg_size"] != args.msg_size:
                    continue
                if header["phase"] == 0:
                    continue
                if header["phase"] == 2:
                    with recv_lock:
                        cooldown_seen.add(index)
                        if len(cooldown_seen) >= len(clients):
                            cooldown_ready.set()
                    continue
                if not collect_active[0]:
                    continue
                now = time.perf_counter()
                if now > active_deadline[0]:
                    continue
                if not is_active_message(
                    data,
                    expected_msg_size=args.msg_size,
                    run_id=run_id,
                ):
                    continue
                with recv_lock:
                    latencies.append(latency_ns_from_message(data))
                    nonlocal_received[0] += 1

        nonlocal_received = [0]
        for index in range(args.clients):
            _trace(f"create-slot-start index={index}")
            spot = node.create_spot()
            spot.set_routing_id(
                f"a-python-multi-spot-client-spot-{index}".encode("utf-8")
            )
            spot.set_subscription(TOPIC)
            spot.on_dispatch_event(partial(on_dispatch, index))
            clients.append((node, spot))
            _trace(f"create-slot-done index={index}")

        deadline = time.perf_counter() + ready_timeout_s
        while time.perf_counter() < deadline:
            if control_connected.is_set():
                break
            control_connected.wait(WAIT_SLICE_S)
        if not control_connected.is_set():
            raise RuntimeError("control connection handshake timeout")
        _trace("control-connected")

        time.sleep(ready_settle_s)
        time.sleep(control_settle_s)
        ready_sender[0].sendall(b"CONNECTED\n")
        ready_sender[0].sendall(f"READY_COUNT,{args.msg_size},{args.clients}\n".encode("utf-8"))
        print(f"CLIENT_READY,{args.msg_size}", flush=True)
        _trace("client-ready")

        start_deadline = time.perf_counter() + ready_timeout_s
        while time.perf_counter() < start_deadline:
            if runner_start.is_set():
                break
            runner_start.wait(WAIT_SLICE_S)
        if not runner_start.is_set():
            raise RuntimeError("spot start handshake timeout")
        _trace(f"start-handshake-done runner={runner_start.is_set()} broadcast={started_event.is_set()} size={started_size[0]}")

        active_deadline[0] = time.perf_counter() + args.duration
        collect_active[0] = True
        idle_deadline = active_deadline[0] + IDLE_DRAIN_S
        _trace("dispatch-ready")
        while time.perf_counter() < idle_deadline:
            if cooldown_ready.is_set() and time.perf_counter() >= active_deadline[0]:
                break
            time.sleep(WAIT_SLICE_S)
        collect_active[0] = False
        with recv_lock:
            received_count = nonlocal_received[0]
        _trace(f"drain-complete count={received_count} cooldown={len(cooldown_seen)}")

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
        _trace("result-flushed")
        os._exit(0)


if __name__ == "__main__":
    main()
