# SPDX-License-Identifier: MPL-2.0

import ctypes

from ..._native.ffi import ZlinkActorRef as _Ref
from ..._native.ffi import lib
from ...contracts.errors.codes import ConfigResult
from ...contracts.errors.errors import ConfigError, SubmitError
from ...contracts.sockets.codes import RequestResult, SubmitResult
from ..handles.native_support import (
    _REPLY_HANDLER,
    _copy_routing_id,
    _raise_result_error,
    _request_result_from_code,
    _request_result_native_errno,
)
from ..messaging.request_reply import _message_list_from_parts
from ..service.spot import (
    _actor_id_bytes,
    _actor_ref_from_native,
    _actor_ref_to_native,
    _clone_payload as _spot_clone_payload,
    _timeout_to_ms as _spot_timeout_to_ms,
)
from .socket_base import _submit_parts


class StreamActorSupport:
    __slots__ = ("_socket", "_pending", "_reply_handler")

    def __init__(self, socket):
        self._socket = socket
        self._pending = {}
        self._reply_handler = None

    def attach_gateway(self, node):
        rc = lib().zlink_stream_attach_actor_gateway(
            self._socket._handle, node._handle
        )
        if rc != 0:
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())

    def send_bound_actor(self, session_rid, actor_id, parts, flags):
        native_session = _copy_routing_id(session_rid)
        native_parts = _spot_clone_payload(parts)
        rc, err = _submit_parts(
            native_parts,
            lambda part_ptr, part_flag: lib().zlink_stream_send_bound_actor_part(
                self._socket._handle,
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

    def submit_bind_actor(self, session_rid, actor_ref, pending, timeout):
        native_session = _copy_routing_id(session_rid)
        native_actor = _actor_ref_to_native(actor_ref)
        handle = id(pending)
        self._pending[handle] = pending
        rc = lib().zlink_stream_bind_actor(
            self._socket._handle,
            ctypes.byref(native_session),
            ctypes.byref(native_actor),
            self._ensure_reply_handler(),
            ctypes.c_void_p(handle),
            _spot_timeout_to_ms(timeout),
        )
        if rc != 0:
            self._pending.pop(handle, None)
            _raise_result_error(SubmitError, SubmitResult, rc, lib().zlink_errno())

    def submit_unbind_actor(self, session_rid, actor_id, pending, timeout):
        native_session = _copy_routing_id(session_rid)
        handle = id(pending)
        self._pending[handle] = pending
        rc = lib().zlink_stream_unbind_actor(
            self._socket._handle,
            ctypes.byref(native_session),
            _actor_id_bytes(actor_id),
            self._ensure_reply_handler(),
            ctypes.c_void_p(handle),
            _spot_timeout_to_ms(timeout),
        )
        if rc != 0:
            self._pending.pop(handle, None)
            _raise_result_error(SubmitError, SubmitResult, rc, lib().zlink_errno())

    def bound_actors(self, session_rid):
        native_session = _copy_routing_id(session_rid)
        count = ctypes.c_size_t()
        rc = lib().zlink_stream_bound_actors(
            self._socket._handle,
            ctypes.byref(native_session),
            None,
            ctypes.byref(count),
        )
        if rc != 0:
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())
        if count.value == 0:
            return []
        entries = (_Ref * int(count.value))()
        rc = lib().zlink_stream_bound_actors(
            self._socket._handle,
            ctypes.byref(native_session),
            entries,
            ctypes.byref(count),
        )
        if rc != 0:
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())
        return [_actor_ref_from_native(entry) for entry in entries[: int(count.value)]]

    def _ensure_reply_handler(self):
        if self._reply_handler is None:
            self._reply_handler = _REPLY_HANDLER(self._on_reply)
        return self._reply_handler

    def _on_reply(self, result_code, parts, part_count, userdata):
        handle = ctypes.cast(userdata, ctypes.c_void_p).value
        pending = self._pending.pop(handle, None)
        if pending is None:
            return
        result = _request_result_from_code(int(result_code))
        reply = []
        if result == RequestResult.OK:
            reply = _message_list_from_parts(parts, part_count)
        pending.resolve(result, reply, _request_result_native_errno(result))
