# SPDX-License-Identifier: MPL-2.0

try:
    from . import _zlink_native
except ImportError:  # pragma: no cover - exercised when extension is not built.
    _zlink_native = None

try:
    from . import _zlink_perf_native
except ImportError:  # pragma: no cover - exercised when extension is not built.
    _zlink_perf_native = None


def available():
    return _zlink_native is not None


def send_parts(handle, payload, flags):
    if _zlink_native is None:
        return None
    return _zlink_native.send_parts(int(handle), payload, int(flags))


def send_parts_rid(handle, routing_id, payload, flags):
    if _zlink_native is None:
        return None
    return _zlink_native.send_parts_rid(
        int(handle),
        routing_id,
        payload,
        int(flags),
    )


def publish_parts(handle, topic, payload, flags):
    if _zlink_native is None:
        return None
    return _zlink_native.publish_parts(int(handle), topic, payload, int(flags))


def spot_publish_submit_parts(handle, topic, payload, flags):
    if _zlink_native is None:
        return None
    return _zlink_native.spot_publish_submit_parts(
        int(handle),
        topic,
        payload,
        int(flags),
    )


def spot_send_channel_parts(handle, channel_name, payload, flags):
    if _zlink_native is None:
        return None
    return _zlink_native.spot_send_channel_parts(
        int(handle),
        channel_name,
        payload,
        int(flags),
    )


def spot_send_spot_parts(handle, node_rid, spot_rid, payload, flags):
    if _zlink_native is None:
        return None
    return _zlink_native.spot_send_spot_parts(
        int(handle),
        node_rid,
        spot_rid,
        payload,
        int(flags),
    )


def recv_parts(handle, flags):
    if _zlink_native is None:
        return None
    return _zlink_native.recv_parts(int(handle), int(flags))


def subscribe_parts(handle, flags):
    if _zlink_native is None:
        return None
    return _zlink_native.subscribe_parts(int(handle), int(flags))


def router_recv_parts(handle, flags):
    if _zlink_native is None:
        return None
    return _zlink_native.router_recv_parts(int(handle), int(flags))


def spot_subscribe_parts(handle, flags):
    if _zlink_native is None:
        return None
    return _zlink_native.spot_subscribe_parts(int(handle), int(flags))


def spot_recv_parts(handle, flags):
    if _zlink_native is None:
        return None
    return _zlink_native.spot_recv_parts(int(handle), int(flags))


def stream_echo_install(handle, stop_token):
    if _zlink_perf_native is None:
        return None
    return _zlink_perf_native.stream_echo_install(int(handle), stop_token)


def single_socket_one_way(
    sender_handle,
    receiver_handle,
    msg_size,
    duration_s,
    run_id,
    *,
    send_mode=0,
    recv_mode=0,
    aux=None,
):
    if _zlink_perf_native is None:
        return None
    if aux is None:
        return _zlink_perf_native.single_socket_one_way(
            int(sender_handle),
            int(receiver_handle),
            int(msg_size),
            int(duration_s),
            int(run_id),
            int(send_mode),
            int(recv_mode),
        )
    return _zlink_perf_native.single_socket_one_way(
        int(sender_handle),
        int(receiver_handle),
        int(msg_size),
        int(duration_s),
        int(run_id),
        int(send_mode),
        int(recv_mode),
        aux,
    )


def spot_publish_active(
    publisher_handle,
    stop_publisher_handle,
    topic,
    stop_token,
    msg_size,
    duration_s,
    run_id,
):
    if _zlink_perf_native is None:
        return None
    return _zlink_perf_native.spot_publish_active(
        int(publisher_handle),
        int(stop_publisher_handle),
        topic,
        stop_token,
        int(msg_size),
        int(duration_s),
        int(run_id),
    )


def multi_send_one_way(socket_handles, msg_size, duration_s, run_id):
    if _zlink_perf_native is None:
        return None
    return _zlink_perf_native.multi_send_one_way(
        [int(handle) for handle in socket_handles],
        int(msg_size),
        int(duration_s),
        int(run_id),
    )


def multi_echo_roundtrip(
    socket_handles,
    msg_size,
    duration_s,
    run_id,
    *,
    router_mode=False,
    server_routing_id=b"SERVER",
):
    if _zlink_perf_native is None:
        return None
    return _zlink_perf_native.multi_echo_roundtrip(
        [int(handle) for handle in socket_handles],
        int(msg_size),
        int(duration_s),
        int(run_id),
        1 if router_mode else 0,
        server_routing_id,
    )


def multi_spot_sendsend_roundtrip(
    spot_handles,
    msg_size,
    duration_s,
    run_id,
    server_node_rid,
    server_spot_rid,
):
    if _zlink_perf_native is None:
        return None
    return _zlink_perf_native.multi_spot_sendsend_roundtrip(
        [int(handle) for handle in spot_handles],
        int(msg_size),
        int(duration_s),
        int(run_id),
        server_node_rid,
        server_spot_rid,
    )


def multi_spot_reqrep_roundtrip(
    spot_handles,
    msg_size,
    duration_s,
    run_id,
    server_node_rid,
    server_spot_rid,
):
    if _zlink_perf_native is None:
        return None
    return _zlink_perf_native.multi_spot_reqrep_roundtrip(
        [int(handle) for handle in spot_handles],
        int(msg_size),
        int(duration_s),
        int(run_id),
        server_node_rid,
        server_spot_rid,
    )


def spot_routed_echo_install(spot_handle, *, reply_mode=False):
    if _zlink_perf_native is None:
        return None
    session, _received, _sent, _failed, err = _zlink_perf_native.spot_routed_echo_install(
        int(spot_handle),
        1 if reply_mode else 0,
    )
    if err != 0:
        return None
    return session


def spot_routed_echo_stats(session):
    if _zlink_perf_native is None or session is None:
        return None
    return _zlink_perf_native.spot_routed_echo_stats(session)


def router_echo_server_loop(socket_handle, stop_flag):
    if _zlink_perf_native is None:
        return None
    return _zlink_perf_native.router_echo_server_loop(int(socket_handle), stop_flag)


def recv_count_active(socket_handle, msg_size, duration_s, run_id):
    if _zlink_perf_native is None:
        return None
    return _zlink_perf_native.recv_count_active(
        int(socket_handle),
        int(msg_size),
        int(duration_s),
        int(run_id),
    )


def publish_active(socket_handle, topic, stop_token, msg_size, duration_s, run_id):
    if _zlink_perf_native is None:
        return None
    return _zlink_perf_native.publish_active(
        int(socket_handle),
        topic,
        stop_token,
        int(msg_size),
        int(duration_s),
        int(run_id),
    )


def subscribe_count_active(
    socket_handles,
    topic,
    msg_size,
    duration_s,
    run_id,
    sample_stride=32,
):
    if _zlink_perf_native is None:
        return None
    return _zlink_perf_native.subscribe_count_active(
        [int(handle) for handle in socket_handles],
        topic,
        int(msg_size),
        int(duration_s),
        int(run_id),
        int(sample_stride),
    )


def spot_count_install(spot_handle, topic, stop_token, msg_size, run_id):
    if _zlink_perf_native is None:
        return None
    session, err = _zlink_perf_native.spot_count_install(
        int(spot_handle),
        topic,
        stop_token,
        int(msg_size),
        int(run_id),
    )
    if err != 0:
        return None
    return session


def spot_subscribe_count_active(
    spot_handles,
    topic,
    msg_size,
    duration_s,
    run_id,
    sample_stride=32,
):
    if _zlink_perf_native is None:
        return None
    return _zlink_perf_native.spot_subscribe_count_active(
        [int(handle) for handle in spot_handles],
        topic,
        int(msg_size),
        int(duration_s),
        int(run_id),
        int(sample_stride),
    )


def spot_count_start(session, duration_s):
    if _zlink_perf_native is None or session is None:
        return None
    return _zlink_perf_native.spot_count_start(session, int(duration_s))


def spot_count_stats(session, duration_s=1):
    if _zlink_perf_native is None or session is None:
        return None
    return _zlink_perf_native.spot_count_stats(session, int(duration_s))


def stream_echo_drain(session):
    if _zlink_perf_native is None or session is None:
        return None
    return _zlink_perf_native.stream_echo_drain(session)


def stream_echo_stats(session):
    if _zlink_perf_native is None or session is None:
        return None
    return _zlink_perf_native.stream_echo_stats(session)
