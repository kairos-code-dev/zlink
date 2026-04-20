import argparse
import socket
import sys
import threading
import time
from contextlib import ExitStack

import zlink

from perf_multi_common import (
    apply_multi_socket_options,
    HEADER_SIZE,
    benchmark_run_id,
    is_active_message,
    latency_ns_from_message,
    print_result_lines,
    resolve_multi_connect_ready_timeout_ms,
    resolve_multi_spot_control_settle_s,
    resolve_multi_spot_ready_settle_s,
    result_metrics,
    stamp_payload,
    wait_monitor_event,
)


READY_TIMEOUT_S = 15.0
START_TIMEOUT_S = 15.0


def _parse_args(argv):
    parser = argparse.ArgumentParser(prog="perf_multi_spot_reqrep_client.py")
    parser.add_argument("--endpoint", required=True)
    parser.add_argument("--transport", default="tcp")
    parser.add_argument("--duration", type=float, default=5.0)
    parser.add_argument("--msg-size", type=int, default=64)
    parser.add_argument("--clients", type=int, default=100)
    parser.add_argument("--server-node-rid", required=True)
    parser.add_argument("--server-spot-rid", required=True)
    args = parser.parse_args(argv)
    if args.duration <= 0 or args.msg_size < HEADER_SIZE or args.clients <= 0:
        raise SystemExit("invalid perf arguments")
    args.transport = args.transport.lower()
    args.server_node_rid = bytes.fromhex(args.server_node_rid)
    args.server_spot_rid = bytes.fromhex(args.server_spot_rid)
    return args


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
    args = _parse_args(argv or sys.argv[1:])
    if len(args.endpoint.split(",")) != 2:
        raise SystemExit("spot reqrep client expects --endpoint data_ep,control_ep")
    data_endpoint, control_endpoint = args.endpoint.split(",", 1)

    run_id = benchmark_run_id()
    latencies = []
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

    start_reader = socket.create_connection(
        _parse_tcp_endpoint(control_endpoint), timeout=READY_TIMEOUT_S
    )
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
        sockets = [zlink.RouterSocket(ctx) for _ in range(args.clients)]
        payloads = [bytearray(args.msg_size) for _ in range(args.clients)]
        waiting = [False] * args.clients
        pending_count = [0]
        seq_counter = [0]
        try:
            with ExitStack() as stack:
                monitors = []
                for index, sock in enumerate(sockets):
                    sock.set_routing_id(f"SPOT-REQREP-{index}".encode("ascii"))
                    monitor = stack.enter_context(
                        sock.monitor_open(zlink.MonitorEventMask.CONNECTION_READY)
                    )
                    apply_multi_socket_options(sock)
                    sock.connect(data_endpoint)
                    monitors.append(monitor)
                for monitor in monitors:
                    wait_monitor_event(
                        monitor,
                        zlink.MonitorEventMask.CONNECTION_READY,
                        timeout_ms=resolve_multi_connect_ready_timeout_ms(),
                    )

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
                ready_sender[0].sendall(
                    f"READY_COUNT,{args.msg_size},{args.clients}\n".encode("utf-8")
                )
                print(f"CLIENT_READY,{args.msg_size}", flush=True)

                start_deadline = time.perf_counter() + START_TIMEOUT_S
                while time.perf_counter() < start_deadline:
                    if (
                        runner_start.is_set()
                        and started_event.is_set()
                        and started_size[0] == args.msg_size
                    ):
                        break
                    time.sleep(0.01)
                if not (
                    runner_start.is_set()
                    and started_event.is_set()
                    and started_size[0] == args.msg_size
                ):
                    raise RuntimeError("spot reqrep start handshake timeout")

                def on_reply(index, result, reply_parts):
                    try:
                        if result == zlink.RequestResult.OK and reply_parts:
                            data = reply_parts[0].to_bytes()
                            if not is_active_message(
                                data,
                                expected_msg_size=args.msg_size,
                                run_id=run_id,
                            ):
                                return
                            with recv_lock:
                                latencies.append(
                                    latency_ns_from_message(data) / 2.0
                                )
                    finally:
                        for part in reply_parts:
                            part.close()
                        waiting[index] = False
                        pending_count[0] -= 1

                active_deadline = time.perf_counter() + args.duration
                while time.perf_counter() < active_deadline:
                    for index, sock in enumerate(sockets):
                        if waiting[index]:
                            continue
                        seq_counter[0] += 1
                        payload = stamp_payload(
                            payloads[index],
                            phase=1,
                            run_id=run_id,
                            seq=seq_counter[0],
                        )
                        try:
                            sock.request_to_spot(
                                args.server_node_rid,
                                args.server_spot_rid,
                                [bytes(payload)],
                                lambda result, reply_parts, index=index: on_reply(
                                    index, result, reply_parts
                                ),
                                timeout=2.0,
                            )
                        except zlink.SubmitError as exc:
                            if exc.result in (
                                zlink.SubmitResult.BACKPRESSURED,
                                zlink.SubmitResult.NOT_CONNECTED,
                                zlink.SubmitResult.NOT_ADMITTED,
                            ):
                                continue
                            raise
                        waiting[index] = True
                        pending_count[0] += 1

                drain_deadline = time.perf_counter() + 2.0
                while pending_count[0] > 0 and time.perf_counter() < drain_deadline:
                    time.sleep(0.01)

                with recv_lock:
                    collected = list(latencies)
                if not collected:
                    raise RuntimeError(
                        "multi spot reqrep benchmark did not receive any active reply"
                    )
                metrics = result_metrics(
                    count=len(collected),
                    msg_size=args.msg_size,
                    elapsed_s=max(args.duration, 0.001),
                    latencies_ns=collected,
                    bandwidth_multiplier=2.0,
                )
                print_result_lines(
                    "MULTI_SPOT_REQREP", args.transport, args.msg_size, metrics
                )
        finally:
            ready_listener.close()
            start_reader.close()
            for sock in sockets:
                try:
                    sock.close()
                except Exception:
                    pass


if __name__ == "__main__":
    main()
