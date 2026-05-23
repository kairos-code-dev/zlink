import sys
import threading
import time

import zlink

from perf_common import (
    HEADER_MAGIC,
    STOP_TOKEN,
    apply_single_auto_hwm_msg_unit,
    benchmark_run_id,
    apply_single_spot_node_admission,
    configure_single_tls_client,
    configure_single_tls_server,
    decode_header,
    is_stop_token,
    latency_ns_from_message,
    is_active_message,
    new_payload,
    parse_single_args,
    perf_context,
    print_result_lines,
    recv_nonblocking,
    resolve_single_connect_ready_timeout_ms,
    resolve_single_endpoint,
    resolve_single_spot_ready_settle_s,
    result_metrics,
    stamp_payload,
    _env_int,
)


CHANNEL_NAME = "spot-svc"
TOPIC = "bench.topic"


def _spot_publish_blocking(spot, topic, payload):
    """DONTWAIT spot publish with bounded transient-failure attempts."""

    for _ in range(100):
        try:
            return (
                spot.publish(topic)
                .message(payload)
                .flags(zlink.SendFlags.DONT_WAIT)
                .submit()
            )
        except zlink.SubmitError as exc:
            if exc.result not in (
                zlink.SubmitResult.BACKPRESSURED,
                zlink.SubmitResult.NOT_CONNECTED,
            ):
                raise
            time.sleep(0.001)
    return False


def main(argv=None):
    args = parse_single_args(argv or sys.argv[1:], pattern="spot")
    run_id = benchmark_run_id()
    latencies = []
    received = [0]
    probe_ready = threading.Event()
    stop_received = threading.Event()
    recv_lock = threading.Lock()
    active_deadline = [0.0]
    probe_payload = new_payload(args.msg_size)
    active_payload = new_payload(args.msg_size)

    with perf_context() as ctx:
        apply_single_auto_hwm_msg_unit(ctx, args.msg_size)
        # C single perf_spot.cpp: registry / discovery are NULL. The
        # publisher node binds and the subscriber node connect-peers
        # directly; no registry/discovery bootstrap in the measured path.
        with zlink.SpotNode(ctx) as publisher_node:
            with zlink.SpotNode(ctx) as subscriber_node:
                apply_single_spot_node_admission(
                    publisher_node, subscriber_node
                )
                with publisher_node.create_spot() as publisher:
                    with subscriber_node.create_spot() as subscriber:
                        # C perf_spot.cpp: dedicated stop publisher created
                        # on the SUBSCRIBER node so the wire stop token is a
                        # SPOT topic message that does not queue behind the
                        # saturated active remote backlog.
                        with subscriber_node.create_spot() as stop_publisher:
                            publisher_node.set_routing_id(
                                b"z-python-perf-spot-publisher"
                            )
                            subscriber_node.set_routing_id(
                                b"a-python-perf-spot-subscriber"
                            )
                            publisher.set_routing_id(
                                b"z-python-perf-spot-publisher-spot"
                            )
                            subscriber.set_routing_id(
                                b"a-python-perf-spot-subscriber-spot"
                            )
                            stop_publisher.set_routing_id(
                                b"a-python-perf-spot-stop-spot"
                            )
                            publisher_endpoint = resolve_single_endpoint(
                                args.transport, "spot-publisher"
                            )
                            configure_single_tls_server(
                                publisher_node, args.transport
                            )
                            configure_single_tls_client(
                                subscriber_node, args.transport
                            )
                            # C perf_spot.cpp: bind publisher node, then
                            # zlink_spot_node_connect_peer(subscriber_node,
                            # publisher_endpoint) directly (registry=NULL,
                            # discovery=NULL).
                            publisher_node.set_pub_bind(publisher_endpoint)
                            subscriber_node.connect_peer(
                                publisher_node.last_endpoint()
                            )
                            subscriber.set_subscription(TOPIC)

                            def on_dispatch(current_spot, info):
                                if (
                                    info.event
                                    != zlink.SpotDispatchEvent.SUBSCRIBE_READABLE
                                ):
                                    return
                                while True:
                                    try:
                                        msg = recv_nonblocking(
                                            current_spot, method="subscribe"
                                        )
                                    except zlink.RecvError as exc:
                                        if exc.result in (
                                            zlink.RecvResult.TERMINATED,
                                            zlink.RecvResult.INVALID_HANDLE,
                                        ):
                                            return
                                        raise
                                    if msg is None:
                                        return
                                    with msg:
                                        parts = msg.to_bytes_list()
                                    if not parts:
                                        continue
                                    data = parts[0]
                                    # PERF_SINGLE_TEST_POLICY § 1.4:
                                    # exit dispatch / collection on
                                    # wire-level stop token.
                                    if is_stop_token(data):
                                        stop_received.set()
                                        return
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
                                        continue
                                    if not is_active_message(
                                        data,
                                        expected_msg_size=args.msg_size,
                                        run_id=run_id,
                                    ):
                                        continue
                                    if time.perf_counter() > active_deadline[0]:
                                        continue
                                    # C perf_spot.cpp run_active_window:
                                    # every matched header counts
                                    # (received++); single_latency_ns
                                    # clamps clock-skew to 0.0.
                                    latency = latency_ns_from_message(data)
                                    with recv_lock:
                                        received[0] += 1
                                        latencies.append(
                                            latency
                                            if latency is not None
                                            else 0.0
                                        )

                            subscriber.on_dispatch_event(on_dispatch)

                            ready_deadline = time.monotonic() + (
                                resolve_single_connect_ready_timeout_ms()
                                / 1000.0
                            )
                            while (
                                not probe_ready.is_set()
                                and time.monotonic() < ready_deadline
                            ):
                                _spot_publish_blocking(
                                    publisher,
                                    TOPIC,
                                    stamp_payload(
                                        probe_payload,
                                        phase=0,
                                        run_id=run_id,
                                    ),
                                )
                                # Probe-ready handshake remains an
                                # event wait (start gate, not shutdown
                                # synchronization) per
                                # PERF_SINGLE_TEST_POLICY § 1.4.
                                probe_ready.wait(0.05)
                            if not probe_ready.is_set():
                                raise RuntimeError(
                                    "spot benchmark probe-ready timeout"
                                )

                            time.sleep(resolve_single_spot_ready_settle_s())

                            active_deadline[0] = (
                                time.perf_counter() + args.duration
                            )
                            while time.perf_counter() < active_deadline[0]:
                                _spot_publish_blocking(
                                    publisher,
                                    TOPIC,
                                    stamp_payload(
                                        active_payload,
                                        phase=1,
                                        run_id=run_id,
                                    ),
                                )
                            # C perf_spot.cpp: publish the wire stop token
                            # through the dedicated stop publisher on the
                            # subscriber node, not the active publisher.
                            _spot_publish_blocking(
                                stop_publisher, TOPIC, STOP_TOKEN
                            )

                            # Stop can be delayed behind saturated SPOT data
                            # queues on slow binding paths. Keep the measured
                            # active window fixed, then bound shutdown wait so
                            # a lost stop token does not strand the runner.
                            stop_received.wait(
                                _env_int("PERF_SINGLE_STOP_WAIT_MS", 2000)
                                / 1000.0
                            )

                            with recv_lock:
                                collected = list(latencies)
                                total = received[0]
                            if total == 0:
                                raise RuntimeError(
                                    "spot benchmark did not receive any active message"
                                )
                            metrics = result_metrics(
                                count=total,
                                msg_size=args.msg_size,
                                elapsed_s=args.duration,
                                latencies_ns=collected,
                            )
                            print_result_lines(
                                "SPOT", args.transport, args.msg_size, metrics
                            )


if __name__ == "__main__":
    main()
