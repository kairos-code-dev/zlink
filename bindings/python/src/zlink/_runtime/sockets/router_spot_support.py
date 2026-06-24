# SPDX-License-Identifier: MPL-2.0

import ctypes

from ..._native.ffi import lib
from ...contracts.core.routing_id import RoutingId
from ...contracts.errors.errors import SubmitError
from ...contracts.sockets.codes import RequestResult, SubmitResult
from ..handles.native_support import (
    _REPLY_HANDLER,
    _copy_routing_id,
    _raise_result_error,
    _report_unhandled_callback_exception,
    _request_result_from_code,
    _request_result_native_errno,
)
from ..messaging.request_reply import (
    _PendingRequest,
    _ensure_reply_flags_supported,
    _message_list_from_parts,
)
from ..service.spot import (
    _clone_payload as _spot_clone_payload,
    _timeout_to_ms as _spot_timeout_to_ms,
)
from .socket_base import _submit_parts


class RouterSpotSupport:
    __slots__ = ("_socket", "_pending", "_reply_handler")

    def __init__(self, socket):
        self._socket = socket
        self._pending = {}
        self._reply_handler = None

    def has_pending(self):
        return bool(self._pending)

    def send_to_spot(self, dest_node_rid, dest_spot_rid, payload, *, flags=0):
        try:
            native_parts = _spot_clone_payload(payload)
            native_node = _copy_routing_id(dest_node_rid)
            native_spot = _copy_routing_id(dest_spot_rid)
            rc, err = _submit_parts(
                native_parts,
                lambda part_ptr, part_flag: lib().zlink_router_send_spot_part(
                    self._socket._handle,
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

    def request_to_spot(
        self, dest_node_rid, dest_spot_rid, payload, callback, *, flags=0, timeout=0
    ):
        native_parts = _spot_clone_payload(payload)
        native_node = _copy_routing_id(dest_node_rid)
        native_spot = _copy_routing_id(dest_spot_rid)
        reply_handler = self._ensure_reply_handler()

        pending = _PendingRequest(callback=callback)
        handle = id(pending)
        self._pending[handle] = pending
        try:
            rc, err = _submit_parts(
                native_parts,
                lambda part_ptr, part_flag: lib().zlink_router_request_spot_part(
                    self._socket._handle,
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
                self._pending.pop(handle, None)
                _raise_result_error(SubmitError, SubmitResult, rc, err)
            self._socket._request_progress.ensure_running()
            return True
        except SubmitError as ex:
            self._pending.pop(handle, None)
            if int(flags) & 1 and ex.result == SubmitResult.BACKPRESSURED:
                return False
            raise

    def reply_to_spot(
        self, dest_node_rid, dest_spot_rid, request_seq, payload, *, flags=0
    ):
        _ensure_reply_flags_supported(flags)
        native_parts = _spot_clone_payload(payload)
        native_node = _copy_routing_id(dest_node_rid)
        native_spot = _copy_routing_id(dest_spot_rid)
        rc, err = _submit_parts(
            native_parts,
            lambda part_ptr, part_flag: lib().zlink_router_reply_spot_part(
                self._socket._handle,
                ctypes.byref(native_node),
                ctypes.byref(native_spot),
                ctypes.c_uint64(request_seq),
                part_ptr,
                part_flag,
            ),
        )
        if rc != 0:
            _raise_result_error(SubmitError, SubmitResult, rc, err)

    def cancel(self, result):
        for handle, pending in list(self._pending.items()):
            self._pending.pop(handle, None)
            if pending.callback is not None:
                try:
                    pending.callback(result, [])
                except Exception:
                    _report_unhandled_callback_exception(pending.callback)

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
