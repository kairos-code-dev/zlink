import ctypes
import errno
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
TOPIC_BYTES = b"bench.topic"
_NATIVE_SPOT_PART = None


def _native_spot_part_methods():
    global _NATIVE_SPOT_PART
    if _NATIVE_SPOT_PART is None:
        from zlink._native.ffi import ZlinkMsg, ZlinkRoutingId, lib
        from zlink._runtime.core.core import (
            _init_msg_from_buffer,
            _msg_data_ptr,
            _msg_size,
        )

        _NATIVE_SPOT_PART = (
            ZlinkMsg,
            ZlinkRoutingId,
            lib,
            _init_msg_from_buffer,
            _msg_data_ptr,
            _msg_size,
        )
    return _NATIVE_SPOT_PART


def _spot_publish_part_once(spot, topic, payload, flags):
    (
        ZlinkMsg,
        _ZlinkRoutingId,
        lib,
        _init_msg_from_buffer,
        _msg_data_ptr,
        _msg_size,
    ) = _native_spot_part_methods()
    part = ZlinkMsg()
    _init_msg_from_buffer(part, payload, borrow=False)
    rc = lib().zlink_spot_publish_part(
        spot._handle,
        topic,
        ctypes.byref(part),
        int(flags),
        0,
    )
    if rc != 0:
        err = lib().zlink_errno()
        lib().zlink_msg_close(ctypes.byref(part))
        retry_errors = (
            errno.EAGAIN,
            errno.EINTR,
            errno.ENOTCONN,
            errno.EHOSTUNREACH,
            errno.ENETUNREACH,
        )
        if err in retry_errors:
            return False
        raise zlink.SubmitError(zlink.SubmitResult(rc), err)
    return True


def _spot_subscribe_part_once(spot, flags):
    (
        ZlinkMsg,
        ZlinkRoutingId,
        lib,
        _init_msg_from_buffer,
        _msg_data_ptr,
        _msg_size,
    ) = _native_spot_part_methods()
    routing_id = ctypes.POINTER(ZlinkRoutingId)()
    topic_buf = ctypes.create_string_buffer(256)
    topic_len = ctypes.c_size_t(len(topic_buf))
    part = ZlinkMsg()
    has_more = ctypes.c_int()
    rc = lib().zlink_spot_subscribe_part(
        spot._handle,
        ctypes.byref(routing_id),
        topic_buf,
        len(topic_buf),
        ctypes.byref(topic_len),
        ctypes.byref(part),
        ctypes.byref(has_more),
        int(flags),
    )
    if rc != 0:
        err = lib().zlink_errno()
        if err in (errno.EAGAIN, errno.EINTR):
            return None
        raise zlink.RecvError(zlink.RecvResult(rc), err)
    if has_more.value != 0:
        lib().zlink_msg_close(ctypes.byref(part))
        raise RuntimeError("single perf spot native subscribe_part received multipart message")
    topic = bytes(topic_buf.raw[: topic_len.value])
    return lib, part, topic, _msg_data_ptr(part), _msg_size(part)


def _spot_publish_blocking(spot, topic, payload):
    """DONTWAIT spot publish with bounded transient-failure attempts."""

    topic_bytes = topic if isinstance(topic, bytes) else topic.encode("utf-8")
    for _ in range(100):
        if _spot_publish_part_once(
            spot,
            topic_bytes,
            payload,
            zlink.SendFlags.DONT_WAIT,
        ):
            return True
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
                                unpack_from = struct.unpack_from
                                while True:
                                    try:
                                        received_part = _spot_subscribe_part_once(
                                            current_spot,
                                            zlink.RecvFlags.DONT_WAIT,
                                        )
                                    except zlink.RecvError as exc:
                                        if exc.result in (
                                            zlink.RecvResult.TERMINATED,
                                            zlink.RecvResult.INVALID_HANDLE,
                                        ):
                                            return
                                        raise
                                    if received_part is None:
                                        return
                                    lib, part, topic, ptr, size = received_part
                                    try:
                                        if topic != TOPIC_BYTES:
                                            continue
                                        if (
                                            size == len(STOP_TOKEN)
                                            and ctypes.string_at(ptr, size)
                                            == STOP_TOKEN
                                        ):
                                            stop_received.set()
                                            return
                                        if size < HEADER_SIZE:
                                            continue
                                        header_data = ctypes.string_at(
                                            ptr, HEADER_SIZE
                                        )
                                        (
                                            magic,
                                            hdr_run_id,
                                            phase,
                                            hdr_msg_size,
                                            _seq,
                                            sent_ts_ns,
                                        ) = unpack_from(
                                            HEADER_FORMAT, header_data, 0
                                        )
                                        if (
                                            magic != HEADER_MAGIC
                                            or hdr_msg_size != args.msg_size
                                            or hdr_run_id != run_id
                                        ):
                                            continue
                                        if phase == 0:
                                            if not probe_ready.is_set():
                                                probe_ready.set()
                                            continue
                                        if phase != 1:
                                            continue
                                        if time.perf_counter() > active_deadline[0]:
                                            continue
                                        now_ns = time.time_ns()
                                        if sent_ts_ns > 0 and now_ns >= sent_ts_ns:
                                            latency = float(now_ns - sent_ts_ns)
                                        else:
                                            latency = 0.0
                                        with recv_lock:
                                            received[0] += 1
                                            latencies.append(latency)
                                    finally:
                                        lib().zlink_msg_close(ctypes.byref(part))

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
