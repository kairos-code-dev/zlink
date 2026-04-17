# SPDX-License-Identifier: MPL-2.0

import ctypes
import asyncio
import errno
import queue
import threading

from ._enums import RouterOption, SocketType
from ._ffi import ZlinkMsg, lib
from ._core import (
    BindError,
    CloseError,
    ConfigError,
    ConnectError,
    HandlerError,
    Message,
    RecvError,
    RecvResult,
    Received,
    RequestError,
    RequestResult,
    RoutingId,
    SubmitError,
    SubmitResult,
    SubscriptionEvent,
    _copy_routing_id,
    _decode_topic_text,
    _msg_to_bytes,
    _REPLY_HANDLER,
    _ROUTER_HANDLER,
    ZlinkRoutingId,
    _is_eagain,
    _raise_result_error,
    _request_result_from_code,
    _request_result_internal_errno,
    _recv_result_from_errno,
    _submit_result_from_errno,
    _report_unhandled_callback_exception,
    _raise_handler_error_from_errno,
    _routing_id_bytes,
    _raise_recv_error_from_errno,
    _raise_submit_error_from_errno,
    _validated_routing_id_bytes,
)
from ._request_reply import (
    _PendingRequest,
    _clone_payload,
    _ensure_reply_flags_supported,
    _message_list_from_parts,
    _prepare_native_parts,
    _request_received,
    _timeout_to_ms,
)
from ._spot import (
    _close_native_parts_array as _spot_close_native_parts_array,
    _clone_payload as _spot_clone_payload,
    _prepare_native_parts as _spot_prepare_native_parts,
    _timeout_to_ms as _spot_timeout_to_ms,
)
from ._socket_base import (
    _enter_callback,
    _CALLBACK_SENTINEL,
    _BindSocket,
    _DealerOptionSocket,
    _EndpointSocket,
    _DiscoveryAttachSocket,
    _MessageSocket,
    _PublisherOptionSocket,
    _PublisherSocket,
    _RoutingIdSocket,
    _RoutedMessageSocket,
    _RouterOptionSocket,
    _SendReadySocket,
    _Socket,
    _StreamOptionSocket,
    _SubscriberOptionSocket,
    _SubscriberSocket,
    _leave_callback,
)


_STREAM_PACKET_HANDLER = ctypes.CFUNCTYPE(
    None,
    ctypes.c_void_p,
    ctypes.POINTER(ZlinkRoutingId),
    ctypes.POINTER(ZlinkMsg),
    ctypes.POINTER(ZlinkMsg),
    ctypes.c_void_p,
)


_UNSET = object()


class PubSocketOptions:
    _VERBOSE = 0x3301
    _VERBOSER = 0x3302
    _MANUAL = 0x3303
    _NO_DROP = 0x3305

    def __init__(self, socket):
        self._socket = socket

    @property
    def verbose(self):
        return self._socket._get_pub_bool_option(self._VERBOSE)

    @verbose.setter
    def verbose(self, enabled):
        self._socket._set_pub_bool_option(self._VERBOSE, enabled)

    @property
    def verboser(self):
        return self._socket._get_pub_bool_option(self._VERBOSER)

    @verboser.setter
    def verboser(self, enabled):
        self._socket._set_pub_bool_option(self._VERBOSER, enabled)

    @property
    def manual(self):
        return self._socket._get_pub_bool_option(self._MANUAL)

    @manual.setter
    def manual(self, enabled):
        self._socket._set_pub_bool_option(self._MANUAL, enabled)

    @property
    def no_drop(self):
        return self._socket._get_pub_bool_option(self._NO_DROP)

    @no_drop.setter
    def no_drop(self, enabled):
        self._socket._set_pub_bool_option(self._NO_DROP, enabled)


class RouterSocketOptions:
    def __init__(self, socket):
        self._socket = socket

    @property
    def mandatory(self):
        return self._socket._get_router_bool_option(RouterOption.MANDATORY)

    @mandatory.setter
    def mandatory(self, enabled):
        self._socket._set_router_bool_option(RouterOption.MANDATORY, enabled)

    @property
    def handover(self):
        return self._socket._get_router_bool_option(RouterOption.HANDOVER)

    @handover.setter
    def handover(self, enabled):
        self._socket._set_router_bool_option(RouterOption.HANDOVER, enabled)

    @property
    def probe(self):
        return self._socket._get_router_bool_option(RouterOption.PROBE)

    @probe.setter
    def probe(self, enabled):
        self._socket._set_router_bool_option(RouterOption.PROBE, enabled)

    @property
    def connect_routing_id(self):
        cached = getattr(self._socket, "_connect_routing_id_option", None)
        if cached is not None:
            return cached
        return RoutingId(
            self._socket._get_router_bytes_option(RouterOption.CONNECT_ROUTING_ID)
        )

    @connect_routing_id.setter
    def connect_routing_id(self, routing_id):
        typed_routing_id = RoutingId(routing_id)
        self._socket._set_router_bytes_option(
            RouterOption.CONNECT_ROUTING_ID,
            typed_routing_id.to_bytes(),
        )
        self._socket._connect_routing_id_option = typed_routing_id


class PairSocket(_SendReadySocket, _EndpointSocket, _MessageSocket):
    _socket_type_value = SocketType.PAIR


class DealerSocket(
    _SendReadySocket,
    _DiscoveryAttachSocket,
    _EndpointSocket,
    _DealerOptionSocket,
    _RoutingIdSocket,
    _MessageSocket,
):
    _socket_type_value = SocketType.DEALER

    def __init__(self, context):
        super().__init__(context)
        self._request_reply_handler = _REPLY_HANDLER(self._on_request_reply)
        self._pending_requests = {}

    def request(self, payload, *args, timeout=0, flags=_UNSET):
        if len(args) > 1:
            raise TypeError("request() takes at most 2 positional arguments after self")
        if not args:
            if flags is not _UNSET:
                raise TypeError("request() got an unexpected keyword argument 'flags'")
            return self._request_async(payload, timeout=timeout)
        callback = args[0]
        callback_flags = 0 if flags is _UNSET else flags
        self._request_callback(payload, callback, flags=callback_flags, timeout=timeout)
        return None

    def close(self):
        self._cancel_pending_requests(RequestResult.TERMINATED)
        super().close()

    def _request_async(self, payload, *, timeout=0):
        async def _run():
            loop = asyncio.get_running_loop()
            pending = _PendingRequest(loop=loop)
            handle = id(pending)
            self._pending_requests[handle] = pending
            try:
                self._start_request(payload, 0, timeout, handle)
            except Exception:
                self._pending_requests.pop(handle, None)
                raise
            return await pending.future

        return _run()

    def _request_callback(self, payload, callback, *, flags=0, timeout=0):
        pending = _PendingRequest(callback=callback)
        handle = id(pending)
        self._pending_requests[handle] = pending
        try:
            self._start_request(payload, flags, timeout, handle)
        except Exception:
            self._pending_requests.pop(handle, None)
            raise

    def _start_request(self, payload, flags, timeout, handle):
        native_parts = _clone_payload(payload)
        parts_array = _prepare_native_parts(native_parts)
        rc = lib().zlink_dealer_request(
            self._handle,
            parts_array,
            len(native_parts),
            self._request_reply_handler,
            ctypes.c_void_p(handle),
            int(flags),
            _timeout_to_ms(timeout),
        )
        if rc != 0:
            self._pending_requests.pop(handle, None)
            _raise_result_error(SubmitError, SubmitResult, rc, lib().zlink_errno())

    def _on_request_reply(self, result_code, parts, part_count, userdata):
        handle = ctypes.cast(userdata, ctypes.c_void_p).value
        pending = self._pending_requests.pop(handle, None)
        if pending is None:
            return
        result = _request_result_from_code(int(result_code))
        reply = []
        if result == RequestResult.OK:
            reply = _message_list_from_parts(parts, part_count)
        pending.resolve(result, reply, _request_result_internal_errno(result))

    def _cancel_pending_requests(self, result):
        for handle, pending in list(self._pending_requests.items()):
            self._pending_requests.pop(handle, None)
            if pending.future is not None and not pending.future.done():
                pending.loop.call_soon_threadsafe(
                    pending.future.set_exception,
                    RequestError(result, getattr(errno, "ETERM", 156)),
                )
            elif pending.callback is not None:
                try:
                    pending.callback(result, [])
                except Exception:
                    _report_unhandled_callback_exception(pending.callback)


class RouterSocket(
    _SendReadySocket,
    _DiscoveryAttachSocket,
    _EndpointSocket,
    _RouterOptionSocket,
    _RoutingIdSocket,
    _RoutedMessageSocket,
):
    _socket_type_value = SocketType.ROUTER

    def __init__(self, context):
        super().__init__(context)
        self._request_reply_handler = _REPLY_HANDLER(self._on_request_reply)
        self._pending_requests = {}
        self._spot_request_pending = {}
        self._spot_request_reply_handler = None

    @property
    def router_options(self):
        return RouterSocketOptions(self)

    def request(self, routing_id, payload, *args, timeout=0, flags=_UNSET):
        if len(args) > 1:
            raise TypeError("request() takes at most 3 positional arguments after self")
        if not args:
            if flags is not _UNSET:
                raise TypeError("request() got an unexpected keyword argument 'flags'")
            return self._request_async(routing_id, payload, timeout=timeout)
        callback = args[0]
        callback_flags = 0 if flags is _UNSET else flags
        self._request_callback(
            routing_id, payload, callback, flags=callback_flags, timeout=timeout
        )
        return None

    def reply(self, routing_id, request_seq, payload, *, flags=0):
        _ensure_reply_flags_supported(flags)
        native_parts = _clone_payload(payload)
        parts_array = _prepare_native_parts(native_parts)
        native_rid = _copy_routing_id(routing_id)
        rc = lib().zlink_router_reply(
            self._handle,
            ctypes.byref(native_rid),
            ctypes.c_uint64(request_seq),
            parts_array,
            len(native_parts),
        )
        if rc != 0:
            _raise_result_error(SubmitError, SubmitResult, rc, lib().zlink_errno())

    def recv(self, *, flags=0):
        source_node_rid = ctypes.POINTER(ZlinkRoutingId)()
        source_spot_rid = ctypes.POINTER(ZlinkRoutingId)()
        request_seq = ctypes.c_uint64()
        parts = ctypes.POINTER(ZlinkMsg)()
        part_count = ctypes.c_size_t()
        rc = lib().zlink_router_recv(
            self._handle,
            ctypes.byref(source_node_rid),
            ctypes.byref(source_spot_rid),
            ctypes.byref(request_seq),
            ctypes.byref(parts),
            ctypes.byref(part_count),
            int(flags),
        )
        if rc != 0:
            _raise_result_error(RecvError, RecvResult, rc, lib().zlink_errno())
        source_node = _routing_id_bytes(source_node_rid.contents) if source_node_rid else None
        source_spot = _routing_id_bytes(source_spot_rid.contents) if source_spot_rid else None
        routing_id = RoutingId(source_node) if source_node else None
        spot_rid = RoutingId(source_spot) if source_spot else None
        request_seq_value = int(request_seq.value)
        return _request_received(
            parts,
            int(part_count.value),
            routing_id=routing_id,
            spot_rid=spot_rid,
            request_seq=request_seq_value if request_seq_value != 0 else None,
            reply_sender=lambda payload, *, flags=0, routing_id=routing_id, spot_rid=spot_rid, request_seq=request_seq_value: self._reply_from_receive_context(
                routing_id,
                spot_rid,
                request_seq,
                payload,
                flags=flags,
            ),
        )

    def send_to_spot(self, dest_node_rid, dest_spot_rid, payload, *, flags=0):
        native_parts = _spot_clone_payload(payload)
        parts_array = _spot_prepare_native_parts(native_parts)
        native_node = _copy_routing_id(dest_node_rid)
        native_spot = _copy_routing_id(dest_spot_rid)
        rc = lib().zlink_router_send_spot(
            self._handle,
            ctypes.byref(native_node),
            ctypes.byref(native_spot),
            parts_array,
            len(native_parts),
            int(flags),
        )
        if rc != 0:
            _spot_close_native_parts_array(parts_array, len(native_parts))
            _raise_submit_error_from_errno()

    def _ensure_spot_reply_handler(self):
        if self._spot_request_reply_handler is None:
            self._spot_request_reply_handler = _REPLY_HANDLER(self._on_spot_reply)
        return self._spot_request_reply_handler

    def _on_spot_reply(self, result_code, parts, part_count, userdata):
        handle = ctypes.cast(userdata, ctypes.c_void_p).value
        pending = self._spot_request_pending.pop(handle, None)
        if pending is None:
            return
        result = _request_result_from_code(int(result_code))
        reply = []
        if result == RequestResult.OK:
            reply = _message_list_from_parts(parts, part_count)
        pending.resolve(result, reply, _request_result_internal_errno(result))

    def _on_request_reply(self, result_code, parts, part_count, userdata):
        handle = ctypes.cast(userdata, ctypes.c_void_p).value
        pending = self._pending_requests.pop(handle, None)
        if pending is None:
            return
        result = _request_result_from_code(int(result_code))
        reply = []
        if result == RequestResult.OK:
            reply = _message_list_from_parts(parts, part_count)
        pending.resolve(result, reply, _request_result_internal_errno(result))

    def _reply_from_receive_context(
        self, routing_id, spot_rid, request_seq, payload, *, flags=0
    ):
        if spot_rid is None:
            self.reply(routing_id, request_seq, payload, flags=flags)
            return
        self.reply_to_spot(routing_id, spot_rid, request_seq, payload, flags=flags)

    def _request_async(self, routing_id, payload, *, flags=0, timeout=0):
        async def _run():
            loop = asyncio.get_running_loop()
            pending = _PendingRequest(loop=loop)
            handle = id(pending)
            self._pending_requests[handle] = pending
            try:
                self._start_request(routing_id, payload, flags, timeout, handle)
            except Exception:
                self._pending_requests.pop(handle, None)
                raise
            return await pending.future

        return _run()

    def _request_callback(self, routing_id, payload, callback, *, flags=0, timeout=0):
        pending = _PendingRequest(callback=callback)
        handle = id(pending)
        self._pending_requests[handle] = pending
        try:
            self._start_request(routing_id, payload, flags, timeout, handle)
        except Exception:
            self._pending_requests.pop(handle, None)
            raise

    def _start_request(self, routing_id, payload, flags, timeout, handle):
        native_parts = _clone_payload(payload)
        parts_array = _prepare_native_parts(native_parts)
        native_rid = _copy_routing_id(routing_id)
        rc = lib().zlink_router_request(
            self._handle,
            ctypes.byref(native_rid),
            parts_array,
            len(native_parts),
            self._request_reply_handler,
            ctypes.c_void_p(handle),
            int(flags),
            _timeout_to_ms(timeout),
        )
        if rc != 0:
            self._pending_requests.pop(handle, None)
            _raise_result_error(SubmitError, SubmitResult, rc, lib().zlink_errno())

    def request_to_spot(self, dest_node_rid, dest_spot_rid, payload, *args, timeout=0, flags=_UNSET):
        if len(args) > 1:
            raise TypeError(
                "request_to_spot() takes at most 4 positional arguments after self"
            )
        if not args and flags is not _UNSET:
            raise TypeError(
                "request_to_spot() got an unexpected keyword argument 'flags'"
            )
        native_parts = _spot_clone_payload(payload)
        parts_array = _spot_prepare_native_parts(native_parts)
        native_node = _copy_routing_id(dest_node_rid)
        native_spot = _copy_routing_id(dest_spot_rid)
        reply_handler = self._ensure_spot_reply_handler()

        if not args:
            async def _run():
                pending = _PendingRequest(loop=asyncio.get_running_loop())
                handle = id(pending)
                self._spot_request_pending[handle] = pending
                rc = lib().zlink_router_request_spot(
                    self._handle,
                    ctypes.byref(native_node),
                    ctypes.byref(native_spot),
                    parts_array,
                    len(native_parts),
                    reply_handler,
                    ctypes.c_void_p(handle),
                    0,
                    _spot_timeout_to_ms(timeout),
                )
                if rc != 0:
                    self._spot_request_pending.pop(handle, None)
                    _spot_close_native_parts_array(parts_array, len(native_parts))
                    _raise_result_error(SubmitError, SubmitResult, rc, lib().zlink_errno())
                return await pending.future

            return _run()

        pending = _PendingRequest(callback=args[0])
        handle = id(pending)
        self._spot_request_pending[handle] = pending
        rc = lib().zlink_router_request_spot(
            self._handle,
            ctypes.byref(native_node),
            ctypes.byref(native_spot),
            parts_array,
            len(native_parts),
            reply_handler,
            ctypes.c_void_p(handle),
            int(0 if flags is _UNSET else flags),
            _spot_timeout_to_ms(timeout),
        )
        if rc != 0:
            self._spot_request_pending.pop(handle, None)
            _spot_close_native_parts_array(parts_array, len(native_parts))
            _raise_result_error(SubmitError, SubmitResult, rc, lib().zlink_errno())
        return None

    def reply_to_spot(self, dest_node_rid, dest_spot_rid, request_seq, payload, *, flags=0):
        _ensure_reply_flags_supported(flags)
        native_parts = _spot_clone_payload(payload)
        parts_array = _spot_prepare_native_parts(native_parts)
        native_node = _copy_routing_id(dest_node_rid)
        native_spot = _copy_routing_id(dest_spot_rid)
        rc = lib().zlink_router_reply_spot(
            self._handle,
            ctypes.byref(native_node),
            ctypes.byref(native_spot),
            ctypes.c_uint64(request_seq),
            parts_array,
            len(native_parts),
        )
        if rc != 0:
            _spot_close_native_parts_array(parts_array, len(native_parts))
            _raise_result_error(SubmitError, SubmitResult, rc, lib().zlink_errno())

    def close(self):
        self._cancel_pending_requests(RequestResult.TERMINATED)
        self._cancel_spot_pending_requests(RequestResult.TERMINATED)
        super().close()

    def _cancel_pending_requests(self, result):
        for handle, pending in list(self._pending_requests.items()):
            self._pending_requests.pop(handle, None)
            if pending.future is not None and not pending.future.done():
                pending.loop.call_soon_threadsafe(
                    pending.future.set_exception,
                    RequestError(result, getattr(errno, "ETERM", 156)),
                )
            elif pending.callback is not None:
                try:
                    pending.callback(result, [])
                except Exception:
                    _report_unhandled_callback_exception(pending.callback)

    def _cancel_spot_pending_requests(self, result):
        for handle, pending in list(self._spot_request_pending.items()):
            self._spot_request_pending.pop(handle, None)
            if pending.future is not None and not pending.future.done():
                pending.loop.call_soon_threadsafe(
                    pending.future.set_exception,
                    RequestError(result, getattr(errno, "ETERM", 156)),
                )
            elif pending.callback is not None:
                try:
                    pending.callback(result, [])
                except Exception:
                    _report_unhandled_callback_exception(pending.callback)


class StreamSocket(_SendReadySocket, _BindSocket, _StreamOptionSocket, _RoutingIdSocket, _RoutedMessageSocket):
    _socket_type_value = SocketType.STREAM

    def on_packet(self, handler):
        if handler is None:
            raise ValueError("handler must not be None")
        if self._recv_handler_thread is not None or self._packet_handler_thread is not None:
            raise RuntimeError("handler is already attached")

        stop = threading.Event()
        events = queue.SimpleQueue()
        self._packet_handler = handler
        self._packet_handler_stop = stop
        self._packet_handler_queue = events

        def _callback(_stream, source_rid_ptr, header_ptr, body_ptr, _):
            if stop.is_set():
                return
            try:
                routing_id = None
                if source_rid_ptr:
                    routing_id = _routing_id_bytes(source_rid_ptr.contents)
                header = Message.from_bytes(_msg_to_bytes(header_ptr.contents))
                body = Message.from_bytes(_msg_to_bytes(body_ptr.contents))
                events.put((routing_id, header, body))
            except Exception:
                _report_unhandled_callback_exception(handler)

        callback = _STREAM_PACKET_HANDLER(_callback)
        rc = lib().zlink_stream_packet_handler(self._handle, callback, None)
        if rc != 0:
            self._packet_handler = None
            self._packet_handler_stop = None
            self._packet_handler_queue = None
            _raise_result_error(HandlerError, HandlerResult, rc, lib().zlink_errno())
        self._packet_handler_cb = callback

        def _dispatch():
            while True:
                item = events.get()
                if item is _CALLBACK_SENTINEL:
                    return
                routing_id, header, body = item
                _enter_callback()
                try:
                    handler(routing_id, header, body)
                except Exception:
                    _report_unhandled_callback_exception(handler)
                finally:
                    try:
                        header.close()
                    finally:
                        body.close()
                        _leave_callback()

        thread = threading.Thread(target=_dispatch, name="zlink-stream-packet")
        thread.daemon = True
        self._packet_handler_thread = thread
        thread.start()


class PubSocket(_SendReadySocket, _DiscoveryAttachSocket, _EndpointSocket, _PublisherOptionSocket, _PublisherSocket):
    _socket_type_value = SocketType.PUB

    @property
    def publisher_options(self):
        return PubSocketOptions(self)


class SubSocket(_DiscoveryAttachSocket, _EndpointSocket, _SubscriberOptionSocket, _SubscriberSocket):
    _socket_type_value = SocketType.SUB


class XPubSocket(_SendReadySocket, _EndpointSocket, _PublisherOptionSocket, _PublisherSocket):
    _socket_type_value = SocketType.XPUB

    @property
    def publisher_options(self):
        return PubSocketOptions(self)

    def _subscription_event(self, flags):
        routing_id = ZlinkRoutingId()
        subscribed = ctypes.c_int()
        topic_buf = ctypes.create_string_buffer(256)
        topic_len = ctypes.c_size_t(len(topic_buf))
        rc = lib().zlink_subscription_event(
            self._handle,
            ctypes.byref(routing_id),
            ctypes.byref(subscribed),
            topic_buf,
            ctypes.byref(topic_len),
            flags,
        )
        if rc != 0:
            _raise_result_error(RecvError, RecvResult, rc, lib().zlink_errno())
        return SubscriptionEvent(
            routing_id=_routing_id_bytes(routing_id),
            topic=_decode_topic_text(topic_buf.raw[: topic_len.value]),
            subscribed=bool(subscribed.value),
            service_name=None,
        )

    def receive_subscription_event(self, *, flags=0):
        return self._subscription_event(flags)


class XSubSocket(_EndpointSocket, _SubscriberOptionSocket, _SubscriberSocket):
    _socket_type_value = SocketType.XSUB


for _socket_type, _socket_cls in (
    (SocketType.PAIR, PairSocket),
    (SocketType.DEALER, DealerSocket),
    (SocketType.ROUTER, RouterSocket),
    (SocketType.STREAM, StreamSocket),
    (SocketType.PUB, PubSocket),
    (SocketType.SUB, SubSocket),
    (SocketType.XPUB, XPubSocket),
    (SocketType.XSUB, XSubSocket),
):
    _Socket._register_socket_type(_socket_type, _socket_cls)
