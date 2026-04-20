import sys
import threading
import time

import zlink

from perf_common import (
    HEADER_MAGIC,
    benchmark_run_id,
    decode_header,
    is_active_message,
    new_payload,
    parse_single_args,
    print_result_lines,
    resolve_single_endpoint,
    resolve_single_spot_ready_settle_s,
    result_metrics,
    stamp_payload,
)


READY_TIMEOUT_S = 5.0


def _issue_request(requester, node_rid, spot_rid, payload, timeout_s):
    reply_holder = {}
    failure_holder = {}
    done = threading.Event()

    def on_reply(result, reply_parts):
        try:
            if result != zlink.RequestResult.OK:
                failure_holder["exc"] = RuntimeError(
                    f"spot request failed: {result!r}"
                )
                return
            reply_holder["parts"] = [part.to_bytes() for part in reply_parts]
        finally:
            for part in reply_parts:
                part.close()
            done.set()

    requester.request_to_spot(
        node_rid,
        spot_rid,
        [bytes(payload)],
        on_reply,
        timeout=timeout_s,
    )
    if not done.wait(timeout_s):
        raise RuntimeError("spot request timed out")
    if "exc" in failure_holder:
        raise failure_holder["exc"]
    return reply_holder.get("parts", [])


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

                    reply_parts = _issue_request(
                        requester,
                        replier_node.routing_id,
                        replier.routing_id,
                        stamp_payload(probe_payload, phase=0, run_id=run_id),
                        READY_TIMEOUT_S,
                    )
                    if not probe_ready.is_set() or not reply_parts:
                        raise RuntimeError("spot reqrep probe-ready timeout")

                    time.sleep(resolve_single_spot_ready_settle_s())

                    active_end = time.monotonic() + args.duration
                    while time.monotonic() < active_end:
                        sent_payload = stamp_payload(
                            active_payload,
                            phase=1,
                            run_id=run_id,
                        )
                        reply_parts = _issue_request(
                            requester,
                            replier_node.routing_id,
                            replier.routing_id,
                            sent_payload,
                            2.0,
                        )
                        if not reply_parts:
                            continue
                        data = reply_parts[0]
                        if not is_active_message(
                            data,
                            expected_msg_size=args.msg_size,
                            run_id=run_id,
                        ):
                            continue
                        header = decode_header(data)
                        if header is None:
                            continue
                        latencies_ns.append(
                            float(time.time_ns() - header["sent_ts_ns"]) / 2.0
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
