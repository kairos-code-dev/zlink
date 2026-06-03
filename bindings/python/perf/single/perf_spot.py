import sys
import struct
import threading
import time

import zlink

from perf_common import (
    HEADER_SIZE,
    HEADER_MAGIC,
    STOP_TOKEN,
    apply_single_auto_hwm_msg_unit,
    benchmark_run_id,
    apply_single_spot_node_admission,
    configure_single_tls_client,
    configure_single_tls_server,
    new_payload,
    parse_single_args,
    perf_context,
    poll_idle_ms,
    print_result_lines,
    resolve_single_connect_ready_timeout_ms,
    resolve_single_endpoint,
    resolve_single_spot_ready_settle_s,
    result_metrics,
    stamp_payload,
    _env_int,
)
from perf_metrics import HEADER_FORMAT


CHANNEL_NAME = "spot-svc"
TOPIC = "bench.topic"


def _spot_publish_blocking(spot, topic, payload):
    """DONTWAIT spot publish with bounded transient-failure attempts."""

    for _ in range(100):
        try:
            if (
                spot.publish(topic)
                .message(payload)
                .flags(zlink.SendFlags.DONT_WAIT)
                .submit()
            ):
                return True
        except zlink.SubmitError as exc:
            if exc.result != zlink.SubmitResult.BACKPRESSURED:
                raise
        poll_idle_ms(1)
    return False


def _spot_subscribe_once(spot, storage):
    try:
        if not spot.subscribe_into(storage, flags=zlink.RecvFlags.DONT_WAIT):
            return None
        return storage
    except zlink.RecvError as exc:
        if exc.result == zlink.RecvResult.NO_DATA:
            return None
        if exc.result in (
            zlink.RecvResult.TERMINATED,
            zlink.RecvResult.INVALID_HANDLE,
        ):
            return None
        raise


def _read_spot_messages(
    subscriber,
    *,
    args,
    run_id,
    probe_ready,
    stop_received,
    active_deadline,
    recv_lock,
    received,
    latencies,
):
    unpack_from = struct.unpack_from
    message = zlink.create_topic_message()
    with zlink.create_poller() as poller:
        poller.add_socket(subscriber, zlink.PollEventFlag.POLLIN, 0)
        poll_events = zlink.create_poll_events(1)
        while not stop_received.is_set():
            received_message = _spot_subscribe_once(subscriber, message)
            if received_message is None:
                poller.wait(poll_events, 2)
                continue
            try:
                if received_message.topic != TOPIC:
                    continue
                part = received_message.first_part()
                data = part.data
                size = len(data)
                if size == len(STOP_TOKEN) and bytes(data) == STOP_TOKEN:
                    stop_received.set()
                    return
                if size < HEADER_SIZE:
                    continue
                (
                    magic,
                    hdr_run_id,
                    phase,
                    hdr_msg_size,
                    _seq,
                    sent_ts_ns,
                ) = unpack_from(HEADER_FORMAT, data, 0)
                if (
                    magic != HEADER_MAGIC
                    or hdr_msg_size != args.msg_size
                    or hdr_run_id != run_id
                ):
                    continue
                if phase == 0:
                    probe_ready.set()
                    continue
                if phase != 1 or time.perf_counter() > active_deadline[0]:
                    continue
                now_ns = time.time_ns()
                latency = (
                    float(now_ns - sent_ts_ns)
                    if sent_ts_ns > 0 and now_ns >= sent_ts_ns
                    else 0.0
                )
                with recv_lock:
                    received[0] += 1
                    latencies.append(latency)
            finally:
                received_message.close()


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
        with zlink.create_spot_node(ctx) as publisher_node:
            with zlink.create_spot_node(ctx) as subscriber_node:
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
                            receiver = threading.Thread(
                                target=_read_spot_messages,
                                kwargs={
                                    "subscriber": subscriber,
                                    "args": args,
                                    "run_id": run_id,
                                    "probe_ready": probe_ready,
                                    "stop_received": stop_received,
                                    "active_deadline": active_deadline,
                                    "recv_lock": recv_lock,
                                    "received": received,
                                    "latencies": latencies,
                                },
                                daemon=True,
                            )
                            receiver.start()

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
                            ready = probe_ready.is_set()
                            if not ready:
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
                            # C perf_spot.cpp: publish the wire stop
                            # token through the dedicated stop publisher
                            # on the subscriber node, not the active
                            # publisher.
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
                            receiver.join(timeout=1.0)

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
