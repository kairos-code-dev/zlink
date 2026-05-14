# SPDX-License-Identifier: MPL-2.0

import ctypes
import asyncio
import errno
import queue
import threading

from ._enums import RouterOption, SocketType
from ._ffi import ZLINK_PART_FINAL, ZLINK_PART_MORE, ZlinkMsg, lib
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
    _RequestProgressPump,
    _clone_payload,
    _ensure_reply_flags_supported,
    _message_list_from_parts,
    _prepare_native_parts,
    _request_received,
    _timeout_to_ms,
)
from ._spot import (
    _actor_id_bytes,
    _actor_ref_to_native,
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
    _read_int32,
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



def _part_flag(part_index, part_count):
    return ZLINK_PART_FINAL if part_index == part_count - 1 else ZLINK_PART_MORE


def _close_native_parts(native_parts, start=0):
    for native in native_parts[start:]:
        lib().zlink_msg_close(ctypes.byref(native))


def _submit_parts(native_parts, submit_part):
    part_count = len(native_parts)
    for index, native in enumerate(native_parts):
        rc = submit_part(ctypes.byref(native), _part_flag(index, part_count))
        if rc != 0:
            err = lib().zlink_errno()
            _close_native_parts(native_parts, index)
            return rc, err
    return 0, 0


class PubSocketOptions:
    _VERBOSE = 0x3301
    _VERBOSER = 0x3302
    _MANUAL = 0x3303
    _MANUAL_LAST_VALUE = 0x3304
    _NO_DROP = 0x3305
    _WELCOME_MSG = 0x3306
    _TOPICS_COUNT = 0x3307
    _APPROVE_SUBSCRIBE = 0x3308
    _REJECT_SUBSCRIBE = 0x3309

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

    @property
    def manual_last_value(self):
        return self._socket._get_pub_bool_option(self._MANUAL_LAST_VALUE)

    @manual_last_value.setter
    def manual_last_value(self, enabled):
        self._socket._set_pub_bool_option(self._MANUAL_LAST_VALUE, enabled)

    @property
    def welcome_message(self):
        return Message.from_bytes(self._socket._get_pub_option(self._WELCOME_MSG))

    @welcome_message.setter
    def welcome_message(self, message):
        if not isinstance(message, Message):
            message = Message.from_bytes(message)
        self._socket._set_pub_option(self._WELCOME_MSG, message.to_bytes())

    @property
    def topics_count(self):
        return _read_int32(self._socket._get_pub_option(self._TOPICS_COUNT, 4))

    def approve_subscribe(self, routing_id):
        self._socket._set_pub_option(
            self._APPROVE_SUBSCRIBE,
            RoutingId(routing_id).to_bytes(),
        )

    def reject_subscribe(self, routing_id):
        self._socket._set_pub_option(
            self._REJECT_SUBSCRIBE,
            RoutingId(routing_id).to_bytes(),
        )


class RouterSocketOptions:
    # RID_DUPLICATE_POLICY common option values (mirrors ZLINK_RID_DUPLICATE_*)
    _RID_DUPLICATE_POLICY_OPTION = 0x3033
    _RID_DUPLICATE_REJECT = 0
    _RID_DUPLICATE_HANDOVER = 1
    _REQUEST_TIMEOUT_MS = 0x3105

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
        return (
            self._socket._get_common_int_option(self._RID_DUPLICATE_POLICY_OPTION)
            == self._RID_DUPLICATE_HANDOVER
        )

    @handover.setter
    def handover(self, value):
        policy = self._RID_DUPLICATE_HANDOVER if value else self._RID_DUPLICATE_REJECT
        self._socket._set_common_int_option(self._RID_DUPLICATE_POLICY_OPTION, policy)

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

    @property
    def weight(self):
        return self._socket._get_router_int_option(RouterOption.WEIGHT)

    @weight.setter
    def weight(self, value):
        self._socket._set_router_int_option(RouterOption.WEIGHT, value)

    @property
    def request_timeout_ms(self):
        return self._socket._get_router_int_option(self._REQUEST_TIMEOUT_MS)

    @request_timeout_ms.setter
    def request_timeout_ms(self, value):
        self._socket._set_router_int_option(self._REQUEST_TIMEOUT_MS, value)


class PairSocket(_SendReadySocket, _EndpointSocket, _MessageSocket):
    _socket_type_value = SocketType.PAIR

    def send(self):
        from ._spot import SendOp

        return SendOp(
            self,
            lambda parts, op_flags: _MessageSocket.send(self, parts, flags=op_flags),
        )


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
        self._request_progress = _RequestProgressPump(
            lambda: lib().zlink_socket_request_progress_internal(self._handle),
            lambda: bool(self._pending_requests),
        )

    def send(self):
        from ._spot import SendOp

        return SendOp(
            self,
            lambda parts, op_flags: _MessageSocket.send(self, parts, flags=op_flags),
        )

    def request(self):
        from ._spot import RequestOp

        return RequestOp(
            self,
            lambda parts, timeout=0: self._request_async_payload(parts, timeout=timeout),
            lambda parts, callback, flags=0, timeout=0: self._request_callback(
                parts, callback, flags=flags, timeout=timeout
            ),
        )

    async def _request_async_payload(self, payload, *, timeout=0):
        loop = asyncio.get_running_loop()
        pending = _PendingRequest(loop=loop)
        handle = id(pending)
        self._pending_requests[handle] = pending
        try:
            self._start_request(payload, 0, timeout, handle)
        except Exception:
            self._pending_requests.pop(handle, None)
            raise
        self._request_progress.ensure_running()
        return await pending.future

    def close(self):
        self._cancel_pending_requests(RequestResult.TERMINATED)
        super().close()

    def _request_callback(self, payload, callback, *, flags=0, timeout=0):
        pending = _PendingRequest(callback=callback)
        handle = id(pending)
        self._pending_requests[handle] = pending
        try:
            self._start_request(payload, flags, timeout, handle)
            self._request_progress.ensure_running()
            return True
        except SubmitError as ex:
            self._pending_requests.pop(handle, None)
            if int(flags) & 1 and ex.result == SubmitResult.BACKPRESSURED:
                return False
            raise
        except Exception:
            self._pending_requests.pop(handle, None)
            raise

    def _start_request(self, payload, flags, timeout, handle):
        native_parts = _clone_payload(payload)
        rc, err = _submit_parts(
            native_parts,
            lambda part_ptr, part_flag: lib().zlink_dealer_request_part(
                self._handle,
                part_ptr,
                int(flags),
                part_flag,
                _timeout_to_ms(timeout),
                self._request_reply_handler,
                ctypes.c_void_p(handle),
            ),
        )
        if rc != 0:
            self._pending_requests.pop(handle, None)
            _raise_result_error(SubmitError, SubmitResult, rc, err)

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
        self._request_progress = _RequestProgressPump(
            lambda: lib().zlink_socket_request_progress_internal(self._handle),
            lambda: bool(self._pending_requests) or bool(self._spot_request_pending),
        )

    @property
    def router_options(self):
        return RouterSocketOptions(self)

    def send(self, routing_id):
        from ._spot import SendOp

        return SendOp(
            self,
            lambda parts, op_flags: _RoutedMessageSocket.send(
                self, routing_id, parts, flags=op_flags
            ),
        )

    def request(self, peer_rid):
        from ._spot import RequestOp

        return RequestOp(
            self,
            lambda parts, timeout=0: self._request_async_payload(
                peer_rid, parts, timeout=timeout
            ),
            lambda parts, callback, flags=0, timeout=0: self._request_callback(
                peer_rid, parts, callback, flags=flags, timeout=timeout
            ),
        )

    async def _request_async_payload(self, peer_rid, payload, *, timeout=0):
        loop = asyncio.get_running_loop()
        pending = _PendingRequest(loop=loop)
        handle = id(pending)
        self._pending_requests[handle] = pending
        try:
            self._start_request(peer_rid, payload, 0, timeout, handle)
        except Exception:
            self._pending_requests.pop(handle, None)
            raise
        self._request_progress.ensure_running()
        return await pending.future

    def reply(self, routing_id, request_seq):
        from ._spot import ReplyOp

        return ReplyOp(
            lambda parts, op_flags: self._reply_payload(
                routing_id, request_seq, parts, flags=op_flags
            )
        )

    def _reply_payload(self, routing_id, request_seq, payload, *, flags=0):
        _ensure_reply_flags_supported(flags)
        native_parts = _clone_payload(payload)
        native_rid = _copy_routing_id(routing_id)
        rc, err = _submit_parts(
            native_parts,
            lambda part_ptr, part_flag: lib().zlink_router_reply_part(
                self._handle,
                ctypes.byref(native_rid),
                ctypes.c_uint64(request_seq),
                part_ptr,
                part_flag,
            ),
        )
        if rc != 0:
            _raise_result_error(SubmitError, SubmitResult, rc, err)

    def recv(self, received=None, *, flags=0):
        """Canonical caller-provided storage routed recv.

        Pass a long-lived :py:class:`Received` as the first positional
        argument and the binding refills its internal state in place each
        successful call. See ``doc/spec/bindings/README.md`` "Canonical
        Recv: Caller-Provided Storage".

        :param received: Caller-provided :py:class:`Received` storage. When
            provided, returns ``True`` on success or ``False`` when DONTWAIT
            finds no data. When ``None`` (deprecated), allocates and returns
            a fresh :py:class:`Received` or ``None`` on EAGAIN.
        """
        try:
            source_node_rid = ctypes.POINTER(ZlinkRoutingId)()
            source_spot_rid = ctypes.POINTER(ZlinkRoutingId)()
            request_seq = ctypes.c_uint64()
            native_parts = []
            recv_flags = int(flags)
            try:
                while True:
                    native_part = ZlinkMsg()
                    has_more = ctypes.c_int()
                    rc = lib().zlink_router_recv_part(
                        self._handle,
                        ctypes.byref(source_node_rid),
                        ctypes.byref(source_spot_rid),
                        ctypes.byref(request_seq),
                        ctypes.byref(native_part),
                        ctypes.byref(has_more),
                        recv_flags,
                    )
                    if rc != 0:
                        _raise_result_error(RecvError, RecvResult, rc, lib().zlink_errno())
                    native_parts.append(native_part)
                    if has_more.value == 0:
                        break
                    recv_flags = 1
            except Exception:
                _close_native_parts(native_parts)
                raise
        except RecvError as ex:
            if int(flags) & 1 and ex.result == RecvResult.NO_DATA:
                return None if received is None else False
            raise
        part_count = len(native_parts)
        parts_array = (ZlinkMsg * part_count)()
        for index, native_part in enumerate(native_parts):
            parts_array[index] = native_part
        source_node = _routing_id_bytes(source_node_rid.contents) if source_node_rid else None
        source_spot = _routing_id_bytes(source_spot_rid.contents) if source_spot_rid else None
        routing_id = RoutingId(source_node) if source_node else None
        spot_rid = RoutingId(source_spot) if source_spot else None
        request_seq_value = int(request_seq.value)
        socket = self
        request_seq_for_reply = request_seq_value

        def _make_router_send_op():
            from ._spot import SendOp

            return SendOp(
                socket,
                lambda parts, flags: socket._send_op_submit(
                    routing_id, spot_rid, parts, flags
                ),
            )

        def _make_router_reply_op():
            from ._spot import ReplyOp

            return ReplyOp(
                lambda parts, flags: socket._reply_from_receive_context(
                    routing_id, spot_rid, request_seq_for_reply, parts, flags=flags
                )
            )

        fresh = _request_received(
            parts_array,
            part_count,
            routing_id=routing_id,
            spot_rid=spot_rid,
            request_seq=request_seq_value if request_seq_value != 0 else None,
            send_sender=_make_router_send_op,
            reply_sender=_make_router_reply_op,
        )
        if received is None:
            return fresh
        received._adopt_from(fresh)
        return True

    def send_to_spot(self, dest_node_rid, dest_spot_rid):
        from ._spot import SendOp

        return SendOp(
            self,
            lambda parts, op_flags: self._send_to_spot_payload(
                dest_node_rid, dest_spot_rid, parts, flags=op_flags
            ),
        )

    def _send_to_spot_payload(self, dest_node_rid, dest_spot_rid, payload, *, flags=0):
        try:
            native_parts = _spot_clone_payload(payload)
            native_node = _copy_routing_id(dest_node_rid)
            native_spot = _copy_routing_id(dest_spot_rid)
            rc, err = _submit_parts(
                native_parts,
                lambda part_ptr, part_flag: lib().zlink_router_send_spot_part(
                    self._handle,
                    ctypes.byref(native_node),
                    ctypes.byref(native_spot),
                    part_ptr,
                    int(flags),
                    part_flag,
                ),
            )
            if rc != 0:
                _raise_result_error(SubmitError, SubmitResult, rc, err)
            return True
        except SubmitError as ex:
            if int(flags) & 1 and ex.result == SubmitResult.BACKPRESSURED:
                return False
            raise

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
            self._reply_payload(routing_id, request_seq, payload, flags=flags)
            return
        self._reply_to_spot_payload(routing_id, spot_rid, request_seq, payload, flags=flags)

    def _send_op_submit(self, routing_id, spot_rid, parts, flags):
        """Submit a Received.send() payload back to the source. Used by the
        SendOp builder returned from Received.send()."""
        if spot_rid is not None:
            return self._send_to_spot_payload(routing_id, spot_rid, parts, flags=flags)
        return _RoutedMessageSocket.send(self, routing_id, parts, flags=flags)

    def _request_callback(self, routing_id, payload, callback, *, flags=0, timeout=0):
        pending = _PendingRequest(callback=callback)
        handle = id(pending)
        self._pending_requests[handle] = pending
        try:
            self._start_request(routing_id, payload, flags, timeout, handle)
            self._request_progress.ensure_running()
            return True
        except SubmitError as ex:
            self._pending_requests.pop(handle, None)
            if int(flags) & 1 and ex.result == SubmitResult.BACKPRESSURED:
                return False
            raise
        except Exception:
            self._pending_requests.pop(handle, None)
            raise

    def _start_request(self, routing_id, payload, flags, timeout, handle):
        native_parts = _clone_payload(payload)
        native_rid = _copy_routing_id(routing_id)
        rc, err = _submit_parts(
            native_parts,
            lambda part_ptr, part_flag: lib().zlink_router_request_part(
                self._handle,
                ctypes.byref(native_rid),
                part_ptr,
                int(flags),
                part_flag,
                _timeout_to_ms(timeout),
                self._request_reply_handler,
                ctypes.c_void_p(handle),
            ),
        )
        if rc != 0:
            self._pending_requests.pop(handle, None)
            _raise_result_error(SubmitError, SubmitResult, rc, err)

    def request_to_spot(self, dest_node_rid, dest_spot_rid):
        from ._spot import RequestOp

        return RequestOp(
            self,
            lambda parts, timeout=0: self._request_to_spot_async_payload(
                dest_node_rid, dest_spot_rid, parts, timeout=timeout
            ),
            lambda parts, callback, flags=0, timeout=0: self._request_to_spot_callback_payload(
                dest_node_rid,
                dest_spot_rid,
                parts,
                callback,
                flags=flags,
                timeout=timeout,
            ),
        )

    async def _request_to_spot_async_payload(self, dest_node_rid, dest_spot_rid, payload, *, timeout=0):
        native_parts = _spot_clone_payload(payload)
        native_node = _copy_routing_id(dest_node_rid)
        native_spot = _copy_routing_id(dest_spot_rid)
        reply_handler = self._ensure_spot_reply_handler()

        pending = _PendingRequest(loop=asyncio.get_running_loop())
        handle = id(pending)
        self._spot_request_pending[handle] = pending
        rc, err = _submit_parts(
            native_parts,
            lambda part_ptr, part_flag: lib().zlink_router_request_spot_part(
                self._handle,
                ctypes.byref(native_node),
                ctypes.byref(native_spot),
                part_ptr,
                reply_handler,
                ctypes.c_void_p(handle),
                0,
                part_flag,
                _spot_timeout_to_ms(timeout),
            ),
        )
        if rc != 0:
            self._spot_request_pending.pop(handle, None)
            _raise_result_error(SubmitError, SubmitResult, rc, err)
        self._request_progress.ensure_running()
        return await pending.future

    def _request_to_spot_callback_payload(self, dest_node_rid, dest_spot_rid, payload, callback, *, flags=0, timeout=0):
        native_parts = _spot_clone_payload(payload)
        native_node = _copy_routing_id(dest_node_rid)
        native_spot = _copy_routing_id(dest_spot_rid)
        reply_handler = self._ensure_spot_reply_handler()

        pending = _PendingRequest(callback=callback)
        handle = id(pending)
        self._spot_request_pending[handle] = pending
        try:
            rc, err = _submit_parts(
                native_parts,
                lambda part_ptr, part_flag: lib().zlink_router_request_spot_part(
                    self._handle,
                    ctypes.byref(native_node),
                    ctypes.byref(native_spot),
                    part_ptr,
                    reply_handler,
                    ctypes.c_void_p(handle),
                    int(flags),
                    part_flag,
                    _spot_timeout_to_ms(timeout),
                ),
            )
            if rc != 0:
                self._spot_request_pending.pop(handle, None)
                _raise_result_error(SubmitError, SubmitResult, rc, err)
            self._request_progress.ensure_running()
            return True
        except SubmitError as ex:
            self._spot_request_pending.pop(handle, None)
            if int(flags) & 1 and ex.result == SubmitResult.BACKPRESSURED:
                return False
            raise

    def reply_to_spot(self, dest_node_rid, dest_spot_rid, request_seq):
        from ._spot import ReplyOp

        return ReplyOp(
            lambda parts, op_flags: self._reply_to_spot_payload(
                dest_node_rid, dest_spot_rid, request_seq, parts, flags=op_flags
            )
        )

    def _reply_to_spot_payload(self, dest_node_rid, dest_spot_rid, request_seq, payload, *, flags=0):
        _ensure_reply_flags_supported(flags)
        native_parts = _spot_clone_payload(payload)
        native_node = _copy_routing_id(dest_node_rid)
        native_spot = _copy_routing_id(dest_spot_rid)
        rc, err = _submit_parts(
            native_parts,
            lambda part_ptr, part_flag: lib().zlink_router_reply_spot_part(
                self._handle,
                ctypes.byref(native_node),
                ctypes.byref(native_spot),
                ctypes.c_uint64(request_seq),
                part_ptr,
                part_flag,
            ),
        )
        if rc != 0:
            _raise_result_error(SubmitError, SubmitResult, rc, err)

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

    def send(self, routing_id):
        from ._spot import SendOp

        return SendOp(
            self,
            lambda parts, op_flags: _RoutedMessageSocket.send(
                self, routing_id, parts, flags=op_flags
            ),
        )

    def disconnect_rid(self, peer_rid):
        native = _copy_routing_id(peer_rid)
        rc = lib().zlink_disconnect_rid(self._handle, ctypes.byref(native))
        if rc != 0:
            _raise_result_error(ConnectError, ConnectResult, rc, lib().zlink_errno())

    def bind_actor(self, session_rid, actor):
        """Async Actor bind. The stream is bound to its session/actor mapping
        here. A bind does not require nor imply a Spot join."""
        from ._spot import ActorBindOp

        return ActorBindOp(self, session_rid, actor)

    def unbind_actor(self, session_rid, actor_id):
        """Async Actor unbind."""
        from ._spot import ActorUnbindOp

        return ActorUnbindOp(self, session_rid, actor_id)

    def send_bound_actor(self, session_rid, actor_id):
        """Send a payload to the (session, actor) pair bound on this stream."""
        from ._spot import SendOp

        return SendOp(
            self,
            lambda parts, flags: self._send_bound_actor_submit(
                session_rid, actor_id, parts, flags
            ),
        )

    def _send_bound_actor_submit(self, session_rid, actor_id, parts, flags):
        native_session = _copy_routing_id(session_rid)
        native_parts = _spot_clone_payload(parts)
        rc, err = _submit_parts(
            native_parts,
            lambda part_ptr, part_flag: lib().zlink_stream_send_bound_actor_part(
                self._handle,
                ctypes.byref(native_session),
                _actor_id_bytes(actor_id),
                part_ptr,
                int(flags),
                part_flag,
            ),
        )
        if rc != 0:
            if int(flags) & 1 and rc == int(SubmitResult.BACKPRESSURED):
                return False
            _raise_result_error(SubmitError, SubmitResult, rc, err)
        return True

    def _submit_bind_actor(self, session_rid, actor_ref, pending, timeout):
        from ._spot import _PendingRequest  # local import to avoid cycle

        native_session = _copy_routing_id(session_rid)
        native_actor = _actor_ref_to_native(actor_ref)
        handle = id(pending)
        if not hasattr(self, "_actor_request_pending"):
            self._actor_request_pending = {}
        if not hasattr(self, "_actor_reply_handler"):
            self._actor_reply_handler = None
        self._actor_request_pending[handle] = pending
        if self._actor_reply_handler is None:
            self._actor_reply_handler = _REPLY_HANDLER(self._on_actor_reply)
        rc = lib().zlink_stream_bind_actor(
            self._handle,
            ctypes.byref(native_session),
            ctypes.byref(native_actor),
            self._actor_reply_handler,
            ctypes.c_void_p(handle),
            _spot_timeout_to_ms(timeout),
        )
        if rc != 0:
            self._actor_request_pending.pop(handle, None)
            _raise_result_error(SubmitError, SubmitResult, rc, lib().zlink_errno())

    def _submit_unbind_actor(self, session_rid, actor_id, pending, timeout):
        native_session = _copy_routing_id(session_rid)
        handle = id(pending)
        if not hasattr(self, "_actor_request_pending"):
            self._actor_request_pending = {}
        if not hasattr(self, "_actor_reply_handler"):
            self._actor_reply_handler = None
        self._actor_request_pending[handle] = pending
        if self._actor_reply_handler is None:
            self._actor_reply_handler = _REPLY_HANDLER(self._on_actor_reply)
        rc = lib().zlink_stream_unbind_actor(
            self._handle,
            ctypes.byref(native_session),
            _actor_id_bytes(actor_id),
            self._actor_reply_handler,
            ctypes.c_void_p(handle),
            _spot_timeout_to_ms(timeout),
        )
        if rc != 0:
            self._actor_request_pending.pop(handle, None)
            _raise_result_error(SubmitError, SubmitResult, rc, lib().zlink_errno())

    def _on_actor_reply(self, result_code, parts, part_count, userdata):
        handle = ctypes.cast(userdata, ctypes.c_void_p).value
        pending = self._actor_request_pending.pop(handle, None)
        if pending is None:
            return
        result = _request_result_from_code(int(result_code))
        reply = []
        if result == RequestResult.OK:
            reply = _message_list_from_parts(parts, part_count)
        pending.resolve(result, reply, _request_result_internal_errno(result))

    def bound_actors(self, session_rid):
        """Snapshot of Actor refs attached to the given session."""
        from ._ffi import ZlinkActorRef as _Ref
        from ._spot import _actor_ref_from_native

        native_session = _copy_routing_id(session_rid)
        count = ctypes.c_size_t()
        rc = lib().zlink_stream_bound_actors(
            self._handle, ctypes.byref(native_session), None, ctypes.byref(count)
        )
        if rc != 0:
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())
        if count.value == 0:
            return []
        entries = (_Ref * int(count.value))()
        rc = lib().zlink_stream_bound_actors(
            self._handle, ctypes.byref(native_session), entries, ctypes.byref(count)
        )
        if rc != 0:
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())
        return [_actor_ref_from_native(entry) for entry in entries[: int(count.value)]]

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
                    routing_id = RoutingId(_routing_id_bytes(source_rid_ptr.contents))
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

    def publish(self, topic):
        from ._spot import SendOp

        return SendOp(
            self,
            lambda parts, op_flags: _PublisherSocket.publish(
                self, topic, parts, flags=op_flags
            ),
        )


class SubSocket(_DiscoveryAttachSocket, _EndpointSocket, _SubscriberOptionSocket, _SubscriberSocket):
    _socket_type_value = SocketType.SUB


class XPubSocket(_SendReadySocket, _EndpointSocket, _PublisherOptionSocket, _PublisherSocket):
    _socket_type_value = SocketType.XPUB

    @property
    def publisher_options(self):
        return PubSocketOptions(self)

    def publish(self, topic):
        from ._spot import SendOp

        return SendOp(
            self,
            lambda parts, op_flags: _PublisherSocket.publish(
                self, topic, parts, flags=op_flags
            ),
        )

    def _subscription_event(self, flags):
        routing_id = ctypes.POINTER(ZlinkRoutingId)()
        subscribed = ctypes.c_int()
        topic_buf = ctypes.create_string_buffer(256)
        topic_len = ctypes.c_size_t()
        rc = lib().zlink_xpub_recv_part(
            self._handle,
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
            routing_id=_routing_id_bytes(routing_id.contents) if routing_id else None,
            topic=_decode_topic_text(topic_buf.raw[: topic_len.value]),
            subscribed=bool(subscribed.value),
        )

    def receive_subscription_event(self, *, flags=0):
        return self._subscription_event(flags)

    def receive_subscription_event_into(self, event, *, flags=0):
        if event is None or not hasattr(event, "_adopt_from"):
            raise TypeError("event must be a SubscriptionEvent")
        try:
            fresh = self._subscription_event(flags)
        except RecvError as ex:
            if (int(flags) & 1) and ex.result == RecvResult.NO_DATA:
                return False
            raise
        event._adopt_from(fresh)
        return True


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
