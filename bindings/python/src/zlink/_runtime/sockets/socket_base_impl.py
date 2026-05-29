# SPDX-License-Identifier: MPL-2.0

import ctypes
import asyncio
import errno

from ...contracts.sockets.codes import RouterOption, SocketType
from ...contracts.sockets.socket_options import (
    PubSocketOptions,
    RouterSocketOptions,
)
from ..buffers.payload_buffers import _read_int32
from ..._native.ffi import ZLINK_PART_FINAL, ZLINK_PART_MORE, ZlinkMsg, lib
from ..handles.native_support import (
    _copy_routing_id,
    _decode_topic_text,
    _msg_to_bytes,
    _REPLY_HANDLER,
    _ROUTER_HANDLER,
    _ReceivedPartsOwner,
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
from ...contracts.errors.errors import (
    BindError,
    CloseError,
    ConfigError,
    ConnectError,
    HandlerError,
    RecvError,
    RequestError,
    SubmitError,
)
from ...contracts.errors.codes import ConfigResult, ConnectResult
from ...contracts.sockets.codes import HandlerResult, RecvResult, RequestResult, SubmitResult
from ...contracts.core.routing_id import RoutingId
from ..messaging.message_materializer import Message, Received, SubscriptionEvent
from ..messaging.request_reply import (
    _PendingRequest,
    _RequestProgressPump,
    _clone_payload,
    _ensure_reply_flags_supported,
    _message_list_from_parts,
    _prepare_native_parts,
    _timeout_to_ms,
)
from ..service.spot import (
    _actor_id_bytes,
    _actor_ref_to_native,
    _close_native_parts_array as _spot_close_native_parts_array,
    _clone_payload as _spot_clone_payload,
    _prepare_native_parts as _spot_prepare_native_parts,
    _timeout_to_ms as _spot_timeout_to_ms,
)
from .socket_base import (
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
    _STREAM_PACKET_HANDLER,
    _StreamOptionSocket,
    _SubscriberOptionSocket,
    _SubscriberSocket,
    _close_native_parts,
    _part_flag,
    _submit_parts,
)


class PairSocket(_SendReadySocket, _EndpointSocket, _MessageSocket):
    _socket_type_value = SocketType.PAIR

    def send(self):
        from ..service.spot import SendOp

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
            lambda: self._handle,
            lambda: bool(self._pending_requests),
        )

    def send(self):
        from ..service.spot import SendOp

        return SendOp(
            self,
            lambda parts, op_flags: _MessageSocket.send(self, parts, flags=op_flags),
        )

    def request(self):
        from ..service.spot import RequestOp

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
            lambda: self._handle,
            lambda: bool(self._pending_requests) or bool(self._spot_request_pending),
        )

    @property
    def router_options(self):
        return RouterSocketOptions(self)

    def send(self, routing_id):
        from ..service.spot import SendOp

        return SendOp(
            self,
            lambda parts, op_flags: _RoutedMessageSocket.send(
                self, routing_id, parts, flags=op_flags
            ),
        )

    def request(self, peer_rid):
        from ..service.spot import RequestOp

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
        from ..service.spot import ReplyOp

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

    def recv_into(self, received, *, flags=0):
        """Canonical caller-provided storage routed recv.

        Pass a long-lived :py:class:`Received` as the first positional
        argument and the binding refills its internal state in place each
        successful call. See ``doc/spec/bindings/README.md`` "Canonical
        Recv: Caller-Provided Storage".

        :param received: Caller-provided :py:class:`Received` storage.
        :returns: ``True`` on success or ``False`` when DONTWAIT finds no data.
        """
        if received is None:
            raise TypeError("received must be a Received")
        try:
            source_node_rid = ctypes.POINTER(ZlinkRoutingId)()
            source_spot_rid = ctypes.POINTER(ZlinkRoutingId)()
            request_seq = ctypes.c_uint64()
            parts_array = (ZlinkMsg * 1)()
            has_more = ctypes.c_int()
            recv_flags = int(flags)
            native_parts = None
            received_first_part = False
            try:
                rc = lib().zlink_router_recv_part(
                    self._handle,
                    ctypes.byref(source_node_rid),
                    ctypes.byref(source_spot_rid),
                    ctypes.byref(request_seq),
                    ctypes.byref(parts_array[0]),
                    ctypes.byref(has_more),
                    recv_flags,
                )
                if rc != 0:
                    _raise_result_error(RecvError, RecvResult, rc, lib().zlink_errno())
                received_first_part = True
                if has_more.value != 0:
                    native_parts = [parts_array[0]]
                    recv_flags = 1
                    while True:
                        native_part = ZlinkMsg()
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
                    part_count = len(native_parts)
                    parts_array = (ZlinkMsg * part_count)()
                    for index, native_part in enumerate(native_parts):
                        parts_array[index] = native_part
                else:
                    part_count = 1
            except Exception:
                if native_parts is not None:
                    _close_native_parts(native_parts)
                elif received_first_part:
                    lib().zlink_msg_close(ctypes.byref(parts_array[0]))
                raise
        except RecvError as ex:
            if int(flags) & 1 and ex.result == RecvResult.NO_DATA:
                return False
            raise
        source_node = _routing_id_bytes(source_node_rid.contents) if source_node_rid else None
        source_spot = _routing_id_bytes(source_spot_rid.contents) if source_spot_rid else None
        routing_id = RoutingId(source_node) if source_node else None
        spot_rid = RoutingId(source_spot) if source_spot else None
        request_seq_value = int(request_seq.value)
        socket = self
        request_seq_for_reply = request_seq_value

        def _make_router_send_op():
            from ..service.spot import SendOp

            return SendOp(
                socket,
                lambda parts, flags: socket._send_op_submit(
                    routing_id, spot_rid, parts, flags
                ),
            )

        def _make_router_reply_op():
            from ..service.spot import ReplyOp

            return ReplyOp(
                lambda parts, flags: socket._reply_from_receive_context(
                    routing_id, spot_rid, request_seq_for_reply, parts, flags=flags
                )
            )

        received._replace(
            _ReceivedPartsOwner(parts_array, part_count),
            routing_id=routing_id,
            spot_rid=spot_rid,
            request_seq=request_seq_value if request_seq_value != 0 else None,
            send_sender=_make_router_send_op,
            reply_sender=_make_router_reply_op,
        )
        return True

    def send_to_spot(self, dest_node_rid, dest_spot_rid):
        from ..service.spot import SendOp

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
        from ..service.spot import RequestOp

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
        from ..service.spot import ReplyOp

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


class StreamSocket(
    _SendReadySocket,
    _BindSocket,
    _StreamOptionSocket,
    _RoutingIdSocket,
    _RoutedMessageSocket,
):
    _socket_type_value = SocketType.STREAM

    def send(self, routing_id):
        from ..service.spot import SendOp

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

    def attach_actor_gateway(self, node):
        rc = lib().zlink_stream_attach_actor_gateway(self._handle, node._handle)
        if rc != 0:
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())

    def bind_actor(self, session_rid, actor):
        """Async Actor bind. The stream is bound to its session/actor mapping
        here. A bind does not require nor imply a Spot join."""
        from ..service.spot import ActorBindOp

        return ActorBindOp(self, session_rid, actor)

    def unbind_actor(self, session_rid, actor_id):
        """Async Actor unbind."""
        from ..service.spot import ActorUnbindOp

        return ActorUnbindOp(self, session_rid, actor_id)

    def send_bound_actor(self, session_rid, actor_id):
        """Send a payload to the (session, actor) pair bound on this stream."""
        from ..service.spot import SendOp

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
        from ..service.spot import _PendingRequest  # local import to avoid cycle

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
        from ..._native.ffi import ZlinkActorRef as _Ref
        from ..service.spot import _actor_ref_from_native

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
        if self._recv_handler is not None or self._packet_handler is not None:
            raise RuntimeError("handler is already attached")

        self._packet_handler = handler
        dispatcher = self._dispatcher

        def _invoke(routing_id, header, body):
            try:
                handler(routing_id, header, body)
            except Exception:
                _report_unhandled_callback_exception(handler)
            finally:
                try:
                    header.close()
                finally:
                    body.close()

        def _callback(_stream, source_rid_ptr, header_ptr, body_ptr, _):
            try:
                routing_id = None
                if source_rid_ptr:
                    routing_id = RoutingId(_routing_id_bytes(source_rid_ptr.contents))
                header = Message.from_(_msg_to_bytes(header_ptr.contents))
                body = Message.from_(_msg_to_bytes(body_ptr.contents))
            except Exception:
                _report_unhandled_callback_exception(handler)
                return
            dispatcher.submit(
                lambda routing_id=routing_id, header=header, body=body: _invoke(
                    routing_id, header, body
                )
            )

        callback = _STREAM_PACKET_HANDLER(_callback)
        rc = lib().zlink_stream_packet_handler(self._handle, callback, None)
        if rc != 0:
            self._packet_handler = None
            _raise_result_error(HandlerError, HandlerResult, rc, lib().zlink_errno())
        self._packet_handler_cb = callback


class PubSocket(
    _SendReadySocket,
    _DiscoveryAttachSocket,
    _EndpointSocket,
    _PublisherOptionSocket,
    _PublisherSocket,
):
    _socket_type_value = SocketType.PUB

    @property
    def pub_options(self):
        return PubSocketOptions(self)

    def publish(self, topic):
        from ..service.spot import SendOp

        return SendOp(
            self,
            lambda parts, op_flags: _PublisherSocket.publish(
                self, topic, parts, flags=op_flags
            ),
        )


class SubSocket(
    _DiscoveryAttachSocket,
    _EndpointSocket,
    _SubscriberOptionSocket,
    _SubscriberSocket,
):
    _socket_type_value = SocketType.SUB


class XPubSocket(
    _SendReadySocket,
    _EndpointSocket,
    _PublisherOptionSocket,
    _PublisherSocket,
):
    _socket_type_value = SocketType.XPUB

    @property
    def pub_options(self):
        return PubSocketOptions(self)

    def publish(self, topic):
        from ..service.spot import SendOp

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

    def _receive_subscription_event(self, *, flags=0):
        return self._subscription_event(flags)

    def receive_subscription_event_into(self, event, *, flags=0):
        if event is None or not hasattr(event, "_adopt_from"):
            raise TypeError("event must be a SubscriptionEvent")
        try:
            fresh = self._receive_subscription_event(flags=flags)
        except RecvError as ex:
            if (int(flags) & 1) and ex.result == RecvResult.NO_DATA:
                return False
            raise
        event._adopt_from(fresh)
        return True


class XSubSocket(
    _EndpointSocket,
    _SubscriberOptionSocket,
    _SubscriberSocket,
):
    _socket_type_value = SocketType.XSUB


for _public_type in (
    PairSocket,
    DealerSocket,
    RouterSocket,
    StreamSocket,
    PubSocket,
    SubSocket,
    XPubSocket,
    XSubSocket,
):
    _public_type.__module__ = "zlink.contracts.sockets.socket"


def create_pair_socket(context):
    return PairSocket(context)


def create_dealer_socket(context):
    return DealerSocket(context)


def create_router_socket(context):
    return RouterSocket(context)


def create_stream_socket(context):
    return StreamSocket(context)


def create_pub_socket(context):
    return PubSocket(context)


def create_sub_socket(context):
    return SubSocket(context)


def create_xpub_socket(context):
    return XPubSocket(context)


def create_xsub_socket(context):
    return XSubSocket(context)


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
