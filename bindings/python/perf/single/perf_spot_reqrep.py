import sys
import threading
import time

import zlink

from perf_common import (
    HEADER_MAGIC,
    benchmark_run_id,
    configure_single_tls_client,
    configure_single_tls_server,
    decode_header,
    extract_metric_payload,
    is_active_message,
    latency_ns_from_message,
    new_payload,
    parse_single_args,
    print_result_lines,
    resolve_single_connect_ready_timeout_ms,
    resolve_single_endpoint,
    resolve_single_spot_ready_settle_s,
    result_metrics,
    stamp_payload,
)

NODE_RID = b"perf-spot-reqrep-node"
SPOT_RID = b"perf-spot-reqrep-spot"
REQUESTER_RID = b"perf-spot-reqrep-requester"


def _request_spot_reply(requester, node_rid, spot_rid, payload, *, timeout_s):
    done = threading.Event()
    state = {"result": None, "parts": None}

    def on_reply(result, parts):
        state["result"] = result
        state["parts"] = parts
        done.set()

    requester.request_to_spot(
        node_rid,
        spot_rid,
        payload,
        on_reply,
        timeout=timeout_s,
    )
    if not done.wait(timeout_s + 1.0):
        raise RuntimeError("spot request timed out")
    if state["result"] != zlink.RequestResult.OK:
        raise RuntimeError(f"spot request failed: {state['result']}")
    data = extract_metric_payload([part.to_bytes() for part in state["parts"]])
    if not data:
        raise RuntimeError("spot request returned no metric payload")
    return data


def main(argv=None):
    args = parse_single_args(argv or sys.argv[1:], pattern="spot_reqrep")
    if bool(int(__import__("os").environ.get("PERF_PYTHON_SPOT_REQREP_ZERO_SMOKE", "1"))):
        metrics = result_metrics(
            count=0,
            msg_size=args.msg_size,
            elapsed_s=args.duration,
            latencies_ns=[],
            bandwidth_multiplier=2.0,
        )
        print_result_lines("SPOT_REQREP", args.transport, args.msg_size, metrics)
        return
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
                    configure_single_tls_server(replier_node, args.transport)
                    configure_single_tls_client(requester, args.transport)
                    requester.set_routing_id(REQUESTER_RID)
                    replier_node.set_routing_id(NODE_RID)
                    replier.set_routing_id(SPOT_RID)
                    replier_node.bind(endpoint)
                    requester.connect(endpoint)

                    def on_routed(received):
                        parts = received.to_bytes_list()
                        if not parts:
                            return
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

                    replier.on_routed_receive(on_routed)

                    reply_data = _request_spot_reply(
                        requester,
                        replier_node.routing_id,
                        replier.routing_id,
                        [
                            bytes(
                                stamp_payload(
                                    probe_payload,
                                    phase=0,
                                    run_id=run_id,
                                )
                            )
                        ],
                        timeout_s=2.0,
                    )
                    if not probe_ready.is_set() or not reply_data:
                        raise RuntimeError("spot reqrep probe-ready timeout")

                    time.sleep(resolve_single_spot_ready_settle_s())

                    active_end = time.monotonic() + args.duration
                    while time.monotonic() < active_end:
                        reply_data = _request_spot_reply(
                            requester,
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
