import argparse
import socket
import sys
import threading
import time

import zlink

from perf_multi_common import (
    apply_multi_spot_node_admission,
    benchmark_endpoint,
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
)

SERVER_NODE_RID = b"SPOT-REQREP-SERVER-NODE"
SERVER_SPOT_RID = b"SPOT-REQREP-SERVER-SPOT"


def _parse_args(argv):
    parser = argparse.ArgumentParser(prog="perf_multi_spot_reqrep_client.py")
    parser.add_argument("--endpoint", required=True)
    parser.add_argument("--transport", default="tcp")
    parser.add_argument("--duration", type=float, default=5.0)
    parser.add_argument("--msg-size", type=int, default=64)
    parser.add_argument("--clients", type=int, default=100)
    args = parser.parse_args(argv)
    if args.duration <= 0 or args.msg_size < HEADER_SIZE or args.clients <= 0:
        raise SystemExit("invalid perf arguments")
    args.transport = args.transport.lower()
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


def _request_spot_reply(spot, payload, timeout_s):
    done = threading.Event()
    box = {}

    def on_reply(result, messages):
        box["result"] = result
        box["messages"] = messages
        done.set()

    submitted = (
        spot.request_to_spot(SERVER_NODE_RID, SERVER_SPOT_RID)
        .message(bytes(payload))
        .timeout(timeout_s)
        .flags(zlink.SendFlags.DONT_WAIT)
        .submit(on_reply)
    )
    if not submitted:
        return None
    if not done.wait(timeout_s + 0.1):
        return None
    if box.get("result") != zlink.RequestResult.OK:
        return None
    messages = box.get("messages") or []
    if not messages:
        return None
    return messages[0].to_bytes()


def main(argv=None):
    args = _parse_args(argv or sys.argv[1:])
    if len(args.endpoint.split(",")) != 2:
        raise SystemExit("spot reqrep client expects --endpoint data_ep,control_ep")
    data_endpoint, control_endpoint = args.endpoint.split(",", 1)

    run_id = benchmark_run_id()
    latencies = []
    ready_settle_s = resolve_multi_spot_ready_settle_s()
    control_settle_s = resolve_multi_spot_control_settle_s()
    ready_timeout_s = resolve_multi_connect_ready_timeout_ms() / 1000.0
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
        _parse_tcp_endpoint(control_endpoint), timeout=ready_timeout_s
    )
    start_file = start_reader.makefile("r", encoding="utf-8", newline="\n")
    print(f"CONTROL_CONNECTED,{control_endpoint}", flush=True)

    def control_loop():
        for line in start_file:
            text = line.strip()
            if text.startswith("START,"):
                try:
                    started_size[0] = int(text.split(",", 1)[1])
                    started_event.set()
                except ValueError as e:
                    print(f"[spot-reqrep-client] malformed START message: {text!r}: {e}", file=sys.stderr, flush=True)
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
        node = zlink.SpotNode(ctx)
        apply_multi_spot_node_admission(node)
        node.set_routing_id(b"spot-req-client-node")
        node.bind(benchmark_endpoint(args.transport, "multi-spot-reqrep-client"))
        data_endpoint_local = node.last_endpoint()
        node.connect_peer(data_endpoint)
        spots = []
        payloads = [bytearray(args.msg_size) for _ in range(args.clients)]
        seq = 0
        try:
            for index in range(args.clients):
                spot = node.create_spot()
                spot.set_routing_id(f"spot-req-client-spot-{index}".encode("ascii"))
                spots.append(spot)

            # Single blocking wait on control handshake (start gate, not
            # shutdown synchronization).
            if not control_connected.wait(timeout=ready_timeout_s):
                raise RuntimeError("control connection handshake timeout")

            time.sleep(ready_settle_s)
            time.sleep(control_settle_s)
            ready_sender[0].sendall(f"DATA_ENDPOINT,{data_endpoint_local}\n".encode("utf-8"))
            time.sleep(control_settle_s)
            ready_sender[0].sendall(b"CONNECTED\n")

            probe_deadline = time.perf_counter() + ready_timeout_s
            probe = stamp_payload(
                bytearray(args.msg_size),
                phase=0,
                run_id=run_id,
                seq=0,
            )
            while time.perf_counter() < probe_deadline:
                if _request_spot_reply(spots[0], probe, ready_timeout_s) is not None:
                    break
                time.sleep(0.01)
            else:
                raise RuntimeError("spot reqrep probe-ready timeout")

            ready_sender[0].sendall(
                f"READY_COUNT,{args.msg_size},{args.clients}\n".encode("utf-8")
            )
            print(f"CLIENT_READY,{args.msg_size}", flush=True)

            start_deadline = time.perf_counter() + ready_timeout_s
            # Wait for runner START + broadcast START barrier. Both are
            # start gates, so a single blocking wait per event is enough.
            remaining = max(0.0, start_deadline - time.perf_counter())
            runner_start.wait(timeout=remaining)
            remaining = max(0.0, start_deadline - time.perf_counter())
            started_event.wait(timeout=remaining)
            if not (
                runner_start.is_set()
                and started_event.is_set()
                and started_size[0] == args.msg_size
            ):
                raise RuntimeError("spot reqrep start handshake timeout")

            active_deadline = time.perf_counter() + args.duration
            while time.perf_counter() < active_deadline:
                progressed = False
                for index, spot in enumerate(spots):
                    seq += 1
                    payload = stamp_payload(
                        payloads[index],
                        phase=1,
                        run_id=run_id,
                        seq=seq,
                    )
                    data = _request_spot_reply(
                        spot,
                        payload,
                        0.2,
                    )
                    if data is None:
                        continue
                    progressed = True
                    if is_active_message(
                        data,
                        expected_msg_size=args.msg_size,
                        run_id=run_id,
                    ):
                        latencies.append(latency_ns_from_message(data) / 2.0)

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
            print_result_lines(
                "MULTI_SPOT_REQREP", args.transport, args.msg_size, metrics
            )
        finally:
            ready_listener.close()
            start_reader.close()
            for spot in spots:
                try:
                    spot.close()
                except Exception as exc:
                    print(f"[perf] close failed: {exc}", file=sys.stderr)
            node.close()


if __name__ == "__main__":
    main()
