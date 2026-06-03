# SPDX-License-Identifier: MPL-2.0

import ctypes

from ...messaging.message_materializer import (
    Message as _RuntimeMessage,
    Received,
    ReceivedMessage,
    SubscriptionEvent,
    TopicMessage,
)
from ...._native.ffi import ZLINK_PART_FINAL, ZlinkMsg, ZlinkRoutingId, lib
from ...._native import bridge as _native_bridge
from ...handles.native_support import (
    RecvError,
    RecvResult,
    _BytesReceivedPartsOwner,
    _ReceivedPartsOwner,
    _clone_native_msg,
    _decode_topic_text,
    _raise_result_error,
    _routing_id_bytes,
)
from ...sockets.socket_base import _clone_received_owner, _in_callback
from .native_parts import (
    clone_payload as _clone_payload_parts,
    close_native_parts as _close_native_parts,
    prepare_native_parts as _prepare_native_parts,
)


def _payload_parts(payload):
    if isinstance(payload, (list, tuple)):
        parts = payload
    else:
        parts = [payload]
    if not parts:
        raise ValueError("payload must not be empty")
    return parts


def _clone_payload(payload):
    return _clone_payload_parts(_payload_parts(payload))


def _make_received_owner(parts_ptr, part_count):
    parts_array = (ZlinkMsg * part_count)()
    for index in range(part_count):
        parts_array[index] = _clone_native_msg(parts_ptr[index])
    return _clone_received_owner(parts_array, part_count)


def _make_routed_received(
    source_node_rid,
    source_spot_rid,
    request_seq,
    parts_ptr,
    part_count,
    *,
    reply_sender=None,
    send_sender=None,
):
    routing_id = (
        _routing_id_bytes(source_node_rid)
        if source_node_rid is not None
        else None
    )
    spot_routing_id = (
        _routing_id_bytes(source_spot_rid)
        if source_spot_rid is not None
        else None
    )
    owner = _make_received_owner(parts_ptr, int(part_count))
    received = Received(
        owner,
        routing_id=routing_id,
        request_seq=int(request_seq),
        spot_rid=spot_routing_id,
        reply_sender=reply_sender,
        send_sender=send_sender,
    )
    received.source_node_rid = routing_id
    received.source_spot_rid = received.spot_rid
    return received


def _make_owned_routed_received(
    source_node_rid,
    source_spot_rid,
    request_seq,
    parts_array,
    part_count,
    *,
    reply_sender=None,
    send_sender=None,
):
    routing_id = (
        _routing_id_bytes(source_node_rid)
        if source_node_rid is not None
        else None
    )
    spot_routing_id = (
        _routing_id_bytes(source_spot_rid)
        if source_spot_rid is not None
        else None
    )
    received = Received(
        _ReceivedPartsOwner(parts_array, int(part_count)),
        routing_id=routing_id,
        request_seq=int(request_seq),
        spot_rid=spot_routing_id,
        reply_sender=reply_sender,
        send_sender=send_sender,
    )
    received.source_node_rid = routing_id
    received.source_spot_rid = received.spot_rid
    return received


def _make_received(request_seq, parts_ptr, part_count, routing_id=None, *, reply_sender=None):
    owner = _make_received_owner(parts_ptr, int(part_count))
    received = Received(
        owner,
        routing_id=routing_id,
        request_seq=request_seq,
        reply_sender=reply_sender,
    )
    return received


def _make_message_list(parts_ptr, part_count):
    messages = []
    for index in range(int(part_count)):
        msg = _RuntimeMessage.__new__(_RuntimeMessage)
        msg._msg = _clone_native_msg(parts_ptr[index])
        msg._valid = True
        msg._keepalive = None
        messages.append(msg)
    return messages


def _recv_spot_subscribed(handle, flags):
    if not _in_callback():
        bridged = _native_bridge.spot_subscribe_parts(handle, flags)
        if bridged is not None:
            rc, err, routing, topic_raw, parts = bridged
            if int(rc) != 0:
                _raise_result_error(RecvError, RecvResult, rc, err)
            return TopicMessage(
                _decode_topic_text(topic_raw),
                _BytesReceivedPartsOwner._from_trusted_bytes_tuple(parts),
                routing,
            )

    routing_id = None
    native_parts = []
    topic_buf = ctypes.create_string_buffer(256)
    topic_len = 0
    recv_flags = int(flags)
    try:
        while True:
            routing_ptr = ctypes.POINTER(ZlinkRoutingId)()
            current_topic_len = ctypes.c_size_t(len(topic_buf))
            native_part = ZlinkMsg()
            has_more = ctypes.c_int()
            rc = lib().zlink_spot_subscribe_part(
                handle,
                ctypes.byref(routing_ptr),
                topic_buf,
                len(topic_buf),
                ctypes.byref(current_topic_len),
                ctypes.byref(native_part),
                ctypes.byref(has_more),
                recv_flags,
            )
            if rc != 0:
                _raise_result_error(RecvError, RecvResult, rc, lib().zlink_errno())
            if not native_parts:
                if routing_ptr:
                    routing_id = routing_ptr.contents
                topic_len = int(current_topic_len.value)
            native_parts.append(native_part)
            if has_more.value == ZLINK_PART_FINAL:
                break
            recv_flags = 1
    except Exception:
        _close_native_parts(native_parts)
        raise

    owner = _ReceivedPartsOwner(_prepare_native_parts(native_parts), len(native_parts))
    topic = _decode_topic_text(topic_buf.raw[:topic_len])
    return TopicMessage(
        topic,
        owner,
        _routing_id_bytes(routing_id) if routing_id is not None else None,
    )


class SpotSubscribedPart:
    def __init__(self):
        self._topic_raw = None
        self._topic = ""
        self._owner = None
        self.part = None
        self.routing_id = None
        self.has_more = False

    @property
    def topic(self):
        raw = self._topic_raw
        if raw is not None:
            self._topic = raw.decode("utf-8", errors="replace")
            self._topic_raw = None
        return self._topic

    def _replace(self, owner, *, topic_raw=None, routing_id=None, has_more=False):
        self.close()
        self._topic_raw = topic_raw
        self._topic = ""
        self._owner = owner
        self.part = ReceivedMessage._from_owner(owner, 0)
        self.routing_id = routing_id
        self.has_more = bool(has_more)

    def _adopt_from(self, source):
        if source is self:
            return
        self.close()
        self._topic_raw = source._topic_raw
        self._topic = source._topic
        self._owner = source._owner
        self.part = source.part
        self.routing_id = source.routing_id
        self.has_more = source.has_more
        source._topic_raw = None
        source._topic = ""
        source._owner = None
        source.part = None
        source.routing_id = None
        source.has_more = False

    def close(self):
        if self._owner is not None:
            self._owner.close()
        self._owner = None
        self.part = None

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, tb):
        self.close()


def _recv_spot_subscribed_part_into(handle, target, flags):
    routing_id = ctypes.POINTER(ZlinkRoutingId)()
    topic_buf = ctypes.create_string_buffer(256)
    topic_len = ctypes.c_size_t(len(topic_buf))
    parts_array = (ZlinkMsg * 1)()
    has_more = ctypes.c_int()
    rc = lib().zlink_spot_subscribe_part(
        handle,
        ctypes.byref(routing_id),
        topic_buf,
        len(topic_buf),
        ctypes.byref(topic_len),
        ctypes.byref(parts_array[0]),
        ctypes.byref(has_more),
        int(flags),
    )
    if rc != 0:
        _raise_result_error(RecvError, RecvResult, rc, lib().zlink_errno())
    routing = _routing_id_bytes(routing_id.contents) if routing_id else None
    target._replace(
        _ReceivedPartsOwner(parts_array, 1),
        topic_raw=bytes(topic_buf.raw[: topic_len.value]),
        routing_id=routing,
        has_more=has_more.value != ZLINK_PART_FINAL,
    )


def _recv_spot_subscribed_part(handle, flags):
    part = SpotSubscribedPart()
    _recv_spot_subscribed_part_into(handle, part, flags)
    return part


def _recv_spot_subscription_event(handle, flags):
    routing_id = ctypes.POINTER(ZlinkRoutingId)()
    subscribed = ctypes.c_int()
    topic_buf = ctypes.create_string_buffer(256)
    topic_len = ctypes.c_size_t(len(topic_buf))
    rc = lib().zlink_spot_recv_subscription_event(
        handle,
        ctypes.byref(routing_id),
        ctypes.byref(subscribed),
        topic_buf,
        len(topic_buf),
        ctypes.byref(topic_len),
        int(flags),
    )
    if rc != 0:
        _raise_result_error(RecvError, RecvResult, rc, lib().zlink_errno())
    return SubscriptionEvent(
        topic=_decode_topic_text(topic_buf.raw[: topic_len.value]),
        subscribed=bool(subscribed.value),
        routing_id=_routing_id_bytes(routing_id.contents) if routing_id else None,
    )


def _recv_spot_routed(handle, flags, *, reply_sender_factory=None, send_sender_factory=None):
    if not _in_callback():
        bridged = _native_bridge.spot_recv_parts(handle, flags)
        if bridged is not None:
            rc, err, node_rid, spot_rid, request_seq, parts = bridged
            if int(rc) != 0:
                _raise_result_error(RecvError, RecvResult, rc, err)
            request_seq = int(request_seq)
            reply_sender = None
            if reply_sender_factory is not None:
                reply_sender = reply_sender_factory(node_rid, spot_rid, request_seq)
            send_sender = None
            if send_sender_factory is not None:
                send_sender = send_sender_factory(node_rid, spot_rid)
            received = Received(
                _BytesReceivedPartsOwner._from_trusted_bytes_tuple(parts),
                routing_id=node_rid,
                request_seq=request_seq,
                spot_rid=spot_rid,
                reply_sender=reply_sender,
                send_sender=send_sender,
            )
            received.source_node_rid = node_rid
            received.source_spot_rid = received.spot_rid
            return received

    source_node_rid = None
    source_spot_rid = None
    request_seq = 0
    native_parts = []
    recv_flags = int(flags)
    try:
        while True:
            current_source_node_rid = ctypes.POINTER(ZlinkRoutingId)()
            current_source_spot_rid = ctypes.POINTER(ZlinkRoutingId)()
            current_request_seq = ctypes.c_uint64()
            native_part = ZlinkMsg()
            has_more = ctypes.c_int()
            rc = lib().zlink_spot_recv_part(
                handle,
                ctypes.byref(current_source_node_rid),
                ctypes.byref(current_source_spot_rid),
                ctypes.byref(current_request_seq),
                ctypes.byref(native_part),
                ctypes.byref(has_more),
                recv_flags,
            )
            if rc != 0:
                _raise_result_error(RecvError, RecvResult, rc, lib().zlink_errno())
            if not native_parts:
                if current_source_node_rid:
                    source_node_rid = current_source_node_rid.contents
                if current_source_spot_rid:
                    source_spot_rid = current_source_spot_rid.contents
                request_seq = int(current_request_seq.value)
            native_parts.append(native_part)
            if has_more.value == ZLINK_PART_FINAL:
                break
            recv_flags = 1
    except Exception:
        _close_native_parts(native_parts)
        raise

    node_rid = _routing_id_bytes(source_node_rid) if source_node_rid is not None else None
    spot_rid = _routing_id_bytes(source_spot_rid) if source_spot_rid is not None else None
    reply_sender = None
    if reply_sender_factory is not None:
        reply_sender = reply_sender_factory(node_rid, spot_rid, request_seq)
    send_sender = None
    if send_sender_factory is not None:
        send_sender = send_sender_factory(node_rid, spot_rid)

    return _make_owned_routed_received(
        source_node_rid,
        source_spot_rid,
        request_seq,
        _prepare_native_parts(native_parts),
        len(native_parts),
        reply_sender=reply_sender,
        send_sender=send_sender,
    )
