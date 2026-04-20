import sys
import threading
import time

import zlink

from perf_common import (
    HEADER_MAGIC,
    benchmark_run_id,
    decode_header,
    extract_metric_payload,
    is_active_message,
    latency_ns_from_message,
    new_payload,
    parse_single_args,
    print_result_lines,
    recv_nonblocking,
    resolve_single_connect_ready_timeout_ms,
    resolve_single_endpoint,
    resolve_single_spot_ready_settle_s,
    result_metrics,
    safe_poll,
    stamp_payload,
)


def _recv_reply(requester, poller, *, timeout_s):
    deadline = time.perf_counter() + timeout_s
    while time.perf_counter() < deadline:
        timeout_ms = max(1, int((deadline - time.perf_counter()) * 1000))
        events = safe_poll(poller, timeout_ms)
        if not events:
            continue
        for _event in events:
            while True:
                received = recv_nonblocking(requester)
                if received is None:
                    break
                with received:
                    data = extract_metric_payload(received.to_bytes_list())
                if data:
                    return data
    raise RuntimeError("spot request timed out")


def main(argv=None):
    args = parse_single_args(argv or sys.argv[1:], pattern="spot_reqrep")
    run_id = benchmark_run_id()
    latencies_ns = []
    probe_ready = threading.Event()
    probe_payload = new_payload(args.msg_size)
    active_payload = new_payload(args.msg_size)

    with zlink.Context() as ctx:
        with zlink.RouterSocket(ctx) as requester:
            with zlink.SpotNode(ctx) as replier_node:
                with replier_node.create_spot() as replier:
                    endpoint = resolve_single_endpoint(args.transport, "spot-reqrep")
                    replier_node.bind(endpoint)
                    requester.connect(endpoint)
                    with zlink.Poller() as poller:
                        poller.add_socket(requester, zlink.PollEvent.POLLIN)

                        def on_dispatch(current_spot, event):
                            if event != zlink.SpotDispatchEvent.ROUTED_READABLE:
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
                                    parts = received.to_bytes_list()
                                    if not parts:
                                        continue
                                    data = parts[0]
                                    header = decode_header(data)
                                    if (
                                        not probe_ready.is_set()
                                        and header is not None
                                        and header["magic"] == HEADER_MAGIC
                                        and header["phase"] == 0
                                        and header["msg_size"] == args.msg_size
                                        and header["run_id"] == run_id
                                    ):
                                        probe_ready.set()
                                    received.reply(parts)

                        replier.on_dispatch_event(on_dispatch)

                        requester.send_to_spot(
                            replier_node.routing_id,
                            replier.routing_id,
                            [bytes(stamp_payload(probe_payload, phase=0, run_id=run_id))],
                        )
                        reply_data = _recv_reply(
                            requester,
                            poller,
                            timeout_s=resolve_single_connect_ready_timeout_ms()
                            / 1000.0,
                        )
                        if not probe_ready.is_set() or not reply_data:
                            raise RuntimeError("spot reqrep probe-ready timeout")

                        time.sleep(resolve_single_spot_ready_settle_s())

                        active_end = time.monotonic() + args.duration
                        while time.monotonic() < active_end:
                            requester.send_to_spot(
                                replier_node.routing_id,
                                replier.routing_id,
                                [
                                    bytes(
                                        stamp_payload(
                                            active_payload,
                                            phase=1,
                                            run_id=run_id,
                                        )
                                    )
                                ],
                            )
                            reply_data = _recv_reply(
                                requester,
                                poller,
                                timeout_s=2.0,
                            )
                            if time.monotonic() > active_end:
                                continue
                            if not is_active_message(
                                reply_data,
                                expected_msg_size=args.msg_size,
                                run_id=run_id,
                            ):
                                continue
                            latencies_ns.append(
                                latency_ns_from_message(reply_data) / 2.0
                            )

                        if not latencies_ns:
                            raise RuntimeError(
                                "spot reqrep benchmark did not receive any active reply"
                            )
                        metrics = result_metrics(
                            count=len(latencies_ns),
                            msg_size=args.msg_size,
                            elapsed_s=args.duration,
                            latencies_ns=latencies_ns,
                            bandwidth_multiplier=2.0,
                        )
                        print_result_lines(
                            "SPOT_REQREP", args.transport, args.msg_size, metrics
                        )


if __name__ == "__main__":
    main()
