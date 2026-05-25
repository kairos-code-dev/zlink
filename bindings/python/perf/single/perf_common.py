import argparse
import ctypes
import errno
import os
import struct
import sys
import time
from pathlib import Path

PERF_DIR = Path(__file__).resolve().parent.parent
perf_dir_text = str(PERF_DIR)
if perf_dir_text not in sys.path:
    sys.path.insert(0, perf_dir_text)

from perf_stop_token import (
    STOP_TOKEN,
    is_stop_token,
    is_stop_token_in_parts,
)

from perf_metrics import (
    HEADER_MAGIC,
    HEADER_SIZE,
    benchmark_run_id,
    build_report_path,
    configure_tls_client,
    configure_tls_server,
    decode_header,
    extract_metric_payload,
    latency_ns_from_message,
    new_payload,
    parse_result_lines,
    print_result_lines,
    is_active_message,
    payload_phase,
    pin_current_process_cpu0,
    render_effective_options,
    render_markdown_summary,
    result_metrics,
    resolve_single_connect_ready_timeout_ms,
    resolve_single_timeout_seconds,
    rows_by_case,
    safe_poll,
    stamp_payload,
    status_row_text,
    table_header_lines,
    throughput_unit,
    transport_endpoint,
    wait_monitor_event,
    _require_zlink,
)


def parse_single_args(argv, *, pattern):
    parser = argparse.ArgumentParser(prog=f"perf_{pattern.lower()}.py")
    parser.add_argument("--transport", default="tcp")
    parser.add_argument("--duration", type=float, default=5.0)
    parser.add_argument("--msg-size", type=int, default=256)
    args = parser.parse_args(argv)
    if args.duration <= 0:
        raise SystemExit("--duration must be > 0")
    if args.msg_size < HEADER_SIZE:
        raise SystemExit(f"--msg-size must be >= {HEADER_SIZE}")
    args.transport = args.transport.lower()
    return args


def _env_int(name, default):
    value = os.environ.get(name)
    if value in (None, ""):
        return default
    try:
        return int(value)
    except ValueError:
        return default


def perf_context():
    zlink_mod = _require_zlink()
    ctx = zlink_mod.Context()
    io_threads = _env_int("PERF_IO_THREADS", 1)
    if io_threads > 0:
        ctx.options.io_threads = io_threads
    return ctx


def _env_flag(name):
    value = os.environ.get(name)
    return value is not None and value != "" and value == "1"


def bench_single_manual_socket_overrides_allowed():
    # C bench_common_runtime.hpp bench_single_manual_socket_overrides_allowed:
    # numeric HWM/buffers are gated behind these env flags; default path uses
    # context auto-HWM only.
    return _env_flag("PERF_SINGLE_ALLOW_MANUAL_SOCKET_OVERRIDES") or _env_flag(
        "PERF_ALLOW_MANUAL_SOCKET_OVERRIDES"
    )


def resolve_single_send_hwm():
    return _env_int("PERF_SINGLE_SNDHWM", _env_int("PERF_SINGLE_HWM", 0))


def resolve_single_recv_hwm():
    return _env_int("PERF_SINGLE_RCVHWM", _env_int("PERF_SINGLE_HWM", 0))


def resolve_single_send_timeout_ms():
    return _env_int("PERF_SINGLE_SNDTIMEO_MS", -1)


def resolve_single_recv_timeout_ms():
    return _env_int("PERF_SINGLE_RCVTIMEO_MS", 200)


def resolve_single_pubsub_recv_timeout_ms():
    return _env_int(
        "PERF_SINGLE_PUBSUB_RCVTIMEO_MS",
        resolve_single_recv_timeout_ms(),
    )


def resolve_single_pubsub_ready_settle_s():
    return _env_int("PERF_SINGLE_PUBSUB_READY_SETTLE_MS", 1000) / 1000.0


def resolve_single_spot_ready_settle_s():
    return _env_int("PERF_SINGLE_SPOT_READY_SETTLE_MS", 1000) / 1000.0


def resolve_single_endpoint(transport, prefix):
    return transport_endpoint(transport, prefix)


def configure_single_tls_server(target, transport):
    configure_tls_server(target, transport)


def configure_single_tls_client(target, transport):
    configure_tls_client(target, transport)


def apply_single_socket_options(*sockets, receive_timeout_ms=None):
    # C bench_common_runtime.hpp apply_single_hwm: numeric SNDHWM/RCVHWM only
    # applied under PERF_SINGLE_ALLOW_MANUAL_SOCKET_OVERRIDES; the default path
    # relies on context auto-HWM. apply_single_benchmark_socket_options always
    # sets linger=0 and the recv/send timeouts.
    overrides = bench_single_manual_socket_overrides_allowed()
    send_hwm = resolve_single_send_hwm()
    recv_hwm = resolve_single_recv_hwm()
    send_timeout_ms = resolve_single_send_timeout_ms()
    recv_timeout_ms = (
        resolve_single_recv_timeout_ms()
        if receive_timeout_ms is None
        else receive_timeout_ms
    )
    for sock in sockets:
        sock.options.linger_ms = 0
        # bindings/cpp/perf single perf_pair.cpp et al set tcp_no_delay on
        # the raw single sockets so 64B one-way throughput is not pinned by
        # Nagle coalescing. Best-effort: ignore on transports without it.
        try:
            sock.options.tcp_no_delay = True
        except Exception:
            pass
        if overrides:
            if send_hwm > 0:
                sock.options.send_high_water_mark = send_hwm
            if recv_hwm > 0:
                sock.options.receive_high_water_mark = recv_hwm
        if send_timeout_ms >= 0:
            sock.options.send_timeout_ms = send_timeout_ms
        sock.options.receive_timeout_ms = recv_timeout_ms


def apply_single_auto_hwm_msg_unit(ctx, msg_size):
    # Context-level message unit follows the current payload size; numeric
    # socket HWM remains behind the manual-override gate.
    if msg_size <= 0:
        return
    ctx.options.auto_hwm_msg_unit_bytes = msg_size


def apply_single_spot_node_admission(*nodes):
    # C perf_spot.cpp gates admission HWM behind apply_single_hwm; default path
    # leaves SPOT node admission on context auto-HWM.
    if not bench_single_manual_socket_overrides_allowed():
        return
    send_hwm = resolve_single_send_hwm()
    recv_hwm = resolve_single_recv_hwm()
    for node in nodes:
        if send_hwm > 0:
            node.set_pubsub_hwm(send_hwm)
        if recv_hwm > 0:
            node.set_router_hwm(recv_hwm)


def _recv_storage(method):
    zlink_mod = _require_zlink()
    if method == "recv":
        return zlink_mod.Received()
    if method == "subscribe":
        return zlink_mod.TopicMessage()
    raise ValueError(f"unsupported recv method: {method}")


def recv_nonblocking(sock, *, method="recv"):
    zlink_mod = _require_zlink()
    recv_method = sock.subscribe_into if method == "subscribe" else sock.recv_into
    storage = _recv_storage(method)
    try:
        return storage if recv_method(storage, flags=zlink_mod.RecvFlags.DONT_WAIT) else None
    except zlink_mod.RecvError as exc:
        if exc.result == zlink_mod.RecvResult.NO_DATA:
            return None
        raise


def storage_data_part(storage):
    """Return the bytes of the final (data) frame of a received message
    without materializing every frame via to_bytes_list(). The metric
    payload is always the last frame (C recv_single_part_header_flags
    rejects extra/routing frames); router prepends only a routing frame
    as binding metadata, never as a data frame here."""

    parts = storage.parts
    if not parts:
        return b""
    return parts[-1].to_bytes()


def recv_into_storage(sock, storage, *, method="recv", blocking=True):
    """C perf_single_one_way.hpp recv idiom: blocking first recv then a
    DONTWAIT burst-drain, both reusing one caller-owned storage object
    (no poller, no per-message storage allocation). Returns True on a
    received message, False on EAGAIN/NO_DATA."""

    zlink_mod = _require_zlink()
    recv_method = sock.subscribe_into if method == "subscribe" else sock.recv_into
    flags = 0 if blocking else int(zlink_mod.RecvFlags.DONT_WAIT)
    try:
        return bool(recv_method(storage, flags=flags))
    except zlink_mod.RecvError as exc:
        if exc.result == zlink_mod.RecvResult.NO_DATA:
            return False
        raise


def run_one_way_receiver(sock, *, method, msg_size, run_id, active_end,
                         received, latencies):
    """C perf_single_one_way.hpp run_active_phase receiver, fused for the
    Python hot path on one reused storage object. Python uses DONTWAIT for
    the receive loop so a lost stop token cannot leave the runner blocked
    inside native recv. Header is decoded strictly at offset 0 with an exact
    size check; latency = recv_ns - sent_ts_ns clamped to 0.0. Returns the
    updated received count."""

    from perf_metrics import (
        HEADER_FORMAT,
        HEADER_MAGIC,
        HEADER_SIZE,
    )

    zlink_mod = _require_zlink()
    recv_method = sock.subscribe_into if method == "subscribe" else sock.recv_into
    dont_wait = int(zlink_mod.RecvFlags.DONT_WAIT)
    recv_error = zlink_mod.RecvError
    no_data = zlink_mod.RecvResult.NO_DATA
    storage = _recv_storage(method)
    unpack_from = struct.unpack_from
    perf_counter = time.perf_counter
    time_ns = time.time_ns
    count = received
    stop_wait_end = active_end + (_env_int("PERF_SINGLE_STOP_WAIT_MS", 2000) / 1000.0)

    stop_received = False
    while not stop_received:
        if perf_counter() >= stop_wait_end:
            break
        flags = dont_wait
        while True:
            try:
                if not recv_method(storage, flags=flags):
                    if flags == dont_wait and perf_counter() >= active_end:
                        time.sleep(0.001)
                    break
            except recv_error as exc:
                if exc.result == no_data:
                    if flags == dont_wait and perf_counter() >= active_end:
                        time.sleep(0.001)
                    break
                raise
            if perf_counter() >= stop_wait_end:
                stop_received = True
                break
            flags = dont_wait
            parts = storage.parts
            data = parts[-1].to_bytes() if parts else b""
            storage.close()
            if data == STOP_TOKEN:
                stop_received = True
                break
            if len(data) != msg_size or len(data) < HEADER_SIZE:
                continue
            magic, hdr_run_id, phase, hdr_msg_size, _seq, sent_ts_ns = (
                unpack_from(HEADER_FORMAT, data, 0)
            )
            if (
                magic != HEADER_MAGIC
                or phase != 1
                or hdr_msg_size != msg_size
                or hdr_run_id != run_id
            ):
                continue
            if perf_counter() >= active_end:
                continue
            count += 1
            now_ns = time_ns()
            if sent_ts_ns > 0 and now_ns >= sent_ts_ns:
                latencies.append(float(now_ns - sent_ts_ns))
            else:
                latencies.append(0.0)

    return count


def _native_recv_part_methods():
    global _NATIVE_RECV_PART
    if _NATIVE_RECV_PART is None:
        from zlink._native.ffi import ZlinkMsg, ZlinkRoutingId, lib
        from zlink._runtime.core.core import _msg_data_ptr, _msg_size

        _NATIVE_RECV_PART = (
            ZlinkMsg,
            ZlinkRoutingId,
            lib,
            _msg_data_ptr,
            _msg_size,
        )
    return _NATIVE_RECV_PART


def _native_recv_part_once(sock, flags):
    ZlinkMsg, ZlinkRoutingId, lib, _msg_data_ptr, _msg_size = (
        _native_recv_part_methods()
    )
    routing_id = ctypes.POINTER(ZlinkRoutingId)()
    part = ZlinkMsg()
    if lib().zlink_msg_init(ctypes.byref(part)) != 0:
        raise RuntimeError("zlink_msg_init failed")
    has_more = ctypes.c_int()
    rc = lib().zlink_recv_part(
        sock._handle,
        ctypes.byref(routing_id),
        ctypes.byref(part),
        ctypes.byref(has_more),
        int(flags),
    )
    if rc != 0:
        err = lib().zlink_errno()
        lib().zlink_msg_close(ctypes.byref(part))
        zlink_mod = _require_zlink()
        if rc == int(zlink_mod.RecvResult.NO_DATA) or err in (
            errno.EAGAIN,
            errno.EINTR,
        ):
            return None
        raise zlink_mod.RecvError(zlink_mod.RecvResult(rc), err)
    if has_more.value != 0:
        lib().zlink_msg_close(ctypes.byref(part))
        raise RuntimeError("single perf native recv_part received multipart message")
    ptr = _msg_data_ptr(part)
    size = _msg_size(part)
    return lib, part, ptr, size


def run_one_way_receiver_native_recv_part(
    sock, *, msg_size, run_id, active_end, received, latencies
):
    from perf_metrics import HEADER_FORMAT

    zlink_mod = _require_zlink()
    dont_wait = int(zlink_mod.RecvFlags.DONT_WAIT)
    unpack_from = struct.unpack_from
    perf_counter = time.perf_counter
    time_ns = time.time_ns
    count = received
    stop_wait_end = active_end + (_env_int("PERF_SINGLE_STOP_WAIT_MS", 2000) / 1000.0)

    stop_received = False
    while not stop_received:
        if perf_counter() >= stop_wait_end:
            break
        received_part = _native_recv_part_once(sock, dont_wait)
        if received_part is None:
            if perf_counter() >= active_end:
                time.sleep(0.001)
            continue
        lib, part, ptr, size = received_part
        try:
            if size == len(STOP_TOKEN) and ctypes.string_at(ptr, size) == STOP_TOKEN:
                stop_received = True
                continue
            if size != msg_size or size < HEADER_SIZE:
                continue
            data = ctypes.string_at(ptr, HEADER_SIZE)
            magic, hdr_run_id, phase, hdr_msg_size, _seq, sent_ts_ns = (
                unpack_from(HEADER_FORMAT, data, 0)
            )
            if (
                magic != HEADER_MAGIC
                or phase != 1
                or hdr_msg_size != msg_size
                or hdr_run_id != run_id
            ):
                continue
            if perf_counter() >= active_end:
                continue
            count += 1
            now_ns = time_ns()
            if sent_ts_ns > 0 and now_ns >= sent_ts_ns:
                latencies.append(float(now_ns - sent_ts_ns))
            else:
                latencies.append(0.0)
        finally:
            lib().zlink_msg_close(ctypes.byref(part))

    return count


def _native_subscribe_part_methods():
    global _NATIVE_SUBSCRIBE_PART
    if _NATIVE_SUBSCRIBE_PART is None:
        from zlink._native.ffi import ZlinkMsg, ZlinkRoutingId, lib
        from zlink._runtime.core.core import _msg_data_ptr, _msg_size

        _NATIVE_SUBSCRIBE_PART = (
            ZlinkMsg,
            ZlinkRoutingId,
            lib,
            _msg_data_ptr,
            _msg_size,
        )
    return _NATIVE_SUBSCRIBE_PART


def _native_subscribe_part_once(sock, flags, *, topic_capacity=256):
    ZlinkMsg, ZlinkRoutingId, lib, _msg_data_ptr, _msg_size = (
        _native_subscribe_part_methods()
    )
    routing_id = ctypes.POINTER(ZlinkRoutingId)()
    topic_buf = ctypes.create_string_buffer(topic_capacity)
    topic_len = ctypes.c_size_t()
    part = ZlinkMsg()
    if lib().zlink_msg_init(ctypes.byref(part)) != 0:
        raise RuntimeError("zlink_msg_init failed")
    has_more = ctypes.c_int()
    rc = lib().zlink_subscribe_part(
        sock._handle,
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
        lib().zlink_msg_close(ctypes.byref(part))
        zlink_mod = _require_zlink()
        if rc == int(zlink_mod.RecvResult.NO_DATA) or err in (
            errno.EAGAIN,
            errno.EINTR,
        ):
            return None
        raise zlink_mod.RecvError(zlink_mod.RecvResult(rc), err)
    if has_more.value != 0:
        lib().zlink_msg_close(ctypes.byref(part))
        raise RuntimeError("single perf native subscribe_part received multipart message")
    if routing_id:
        lib().zlink_msg_close(ctypes.byref(part))
        raise RuntimeError("single perf native subscribe_part received source routing id")
    topic = bytes(topic_buf.raw[: topic_len.value])
    ptr = _msg_data_ptr(part)
    size = _msg_size(part)
    return lib, part, topic, ptr, size


def run_one_way_subscriber_native_subscribe_part(
    sock, *, topic, msg_size, run_id, active_end, received, latencies
):
    from perf_metrics import HEADER_FORMAT

    zlink_mod = _require_zlink()
    dont_wait = int(zlink_mod.RecvFlags.DONT_WAIT)
    unpack_from = struct.unpack_from
    perf_counter = time.perf_counter
    time_ns = time.time_ns
    count = received
    stop_wait_end = active_end + (_env_int("PERF_SINGLE_STOP_WAIT_MS", 2000) / 1000.0)

    stop_received = False
    while not stop_received:
        if perf_counter() >= stop_wait_end:
            break
        received_part = _native_subscribe_part_once(sock, dont_wait)
        if received_part is None:
            if perf_counter() >= active_end:
                time.sleep(0.001)
            continue
        lib, part, received_topic, ptr, size = received_part
        try:
            if received_topic != topic:
                continue
            if size == len(STOP_TOKEN) and ctypes.string_at(ptr, size) == STOP_TOKEN:
                stop_received = True
                continue
            if size != msg_size or size < HEADER_SIZE:
                continue
            data = ctypes.string_at(ptr, HEADER_SIZE)
            magic, hdr_run_id, phase, hdr_msg_size, _seq, sent_ts_ns = (
                unpack_from(HEADER_FORMAT, data, 0)
            )
            if (
                magic != HEADER_MAGIC
                or phase != 1
                or hdr_msg_size != msg_size
                or hdr_run_id != run_id
            ):
                continue
            if perf_counter() >= active_end:
                continue
            count += 1
            now_ns = time_ns()
            if sent_ts_ns > 0 and now_ns >= sent_ts_ns:
                latencies.append(float(now_ns - sent_ts_ns))
            else:
                latencies.append(0.0)
        finally:
            lib().zlink_msg_close(ctypes.byref(part))

    return count


_DONT_WAIT_FLAG = None
_LOW_LEVEL_SEND = None
_NATIVE_RESULT_SEND = None
_NATIVE_RECV_PART = None
_NATIVE_SUBSCRIBE_PART = None


def _dont_wait_flag():
    global _DONT_WAIT_FLAG
    if _DONT_WAIT_FLAG is None:
        _DONT_WAIT_FLAG = int(_require_zlink().SendFlags.DONT_WAIT)
    return _DONT_WAIT_FLAG


def _low_level_send_methods():
    # The fluent socket.send()/publish() entry points return a per-call
    # SendOp builder, which the profiler flagged as the single largest
    # per-message cost. The socket classes also inherit lower-level
    # message-socket methods that take the payload directly and return a
    # bool; bind to those once and reuse on the hot path.
    global _LOW_LEVEL_SEND
    if _LOW_LEVEL_SEND is None:
        from zlink._runtime.sockets.socket_base import (
            _MessageSocket,
            _PublisherSocket,
            _RoutedMessageSocket,
        )

        _LOW_LEVEL_SEND = (
            _MessageSocket.send,
            _RoutedMessageSocket.send,
            _PublisherSocket.publish,
        )
    return _LOW_LEVEL_SEND


def _native_result_send_methods():
    global _NATIVE_RESULT_SEND
    if _NATIVE_RESULT_SEND is None:
        from zlink._native.ffi import ZlinkMsg
        from zlink._runtime.core.core import (
            _copy_routing_id,
            _init_msg_from_buffer,
        )
        from zlink._runtime.sockets.socket_base import (
            _publish_via_native_no_wait_result,
            _send_rid_via_native_no_wait_result,
            _send_via_native_no_wait_result,
        )

        _NATIVE_RESULT_SEND = (
            ZlinkMsg,
            _init_msg_from_buffer,
            _copy_routing_id,
            _send_via_native_no_wait_result,
            _send_rid_via_native_no_wait_result,
            _publish_via_native_no_wait_result,
        )
    return _NATIVE_RESULT_SEND


def _single_part_native_array(payload):
    ZlinkMsg, init_msg, *_ = _native_result_send_methods()
    parts_array = (ZlinkMsg * 1)()
    keepalive = init_msg(parts_array[0], payload, borrow=False)
    return parts_array, keepalive


def _submit_result_ok(result):
    zlink_mod = _require_zlink()
    return result == zlink_mod.SubmitResult.OK


def send_nonblocking(sock, payload, *, routing_id=None):
    if routing_id is None:
        _ZlinkMsg, _init_msg, _copy_routing_id, send_native, _send_rid = (
            _native_result_send_methods()
        )
        parts_array, keepalive = _single_part_native_array(payload)
        _ = keepalive
        return _submit_result_ok(send_native(sock._handle, parts_array, 1))
    _ZlinkMsg, _init_msg, copy_routing_id, _send_native, send_rid = (
        _native_result_send_methods()
    )
    parts_array, keepalive = _single_part_native_array(payload)
    _ = keepalive
    native_rid = copy_routing_id(routing_id)
    return _submit_result_ok(send_rid(sock._handle, native_rid, parts_array, 1))


def publish_nonblocking(sock, topic, payload):
    _ZlinkMsg, _init_msg, _copy_routing_id, _send_native, _send_rid, publish_native = (
        _native_result_send_methods()
    )
    parts_array, keepalive = _single_part_native_array(payload)
    _ = keepalive
    return _submit_result_ok(publish_native(sock._handle, topic, parts_array, 1))


def single_routing_probe(sender, receiver, payload, *, run_id, msg_size,
                         routing_id=None):
    """C perf_dealer_router.cpp wait_for_dealer_router_ready /
    perf_router_router.cpp wait_for_router_router_ready: one-shot routing
    self-check. Bounded by PERF_CONNECT_READY_TIMEOUT_MS; DONTWAIT probe
    sends (phase=active, seq=0) until the receiver confirms one matching
    header. No retry/sleep loop beyond C's bounded probe."""

    from perf_metrics import decode_header as _decode_header

    deadline = time.monotonic() + (
        resolve_single_connect_ready_timeout_ms() / 1000.0
    )
    probe = stamp_payload(payload, phase=1, run_id=run_id, seq=0)
    while time.monotonic() < deadline:
        send_nonblocking(sender, probe, routing_id=routing_id)
        probe_deadline = min(deadline, time.monotonic() + 0.05)
        while time.monotonic() < probe_deadline:
            received = recv_nonblocking(receiver)
            if received is None:
                time.sleep(0.001)
                continue
            with received:
                parts = received.to_bytes_list()
            data = extract_metric_payload(parts)
            header = _decode_header(data)
            if (
                header is not None
                and header["magic"] == HEADER_MAGIC
                and header["run_id"] == run_id
                and header["phase"] == 1
                and header["msg_size"] == msg_size
            ):
                return True
    return False
