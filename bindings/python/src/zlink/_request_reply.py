# SPDX-License-Identifier: MPL-2.0

import asyncio
import ctypes
import errno
import queue

from ._core import (
    Message,
    RecvError,
    RecvResult,
    Received,
    RequestError,
    RequestResult,
    RoutingId,
    SubmitError,
    SubmitResult,
    ZlinkError,
    ZlinkMsg,
    _REPLY_HANDLER,
    _ROUTER_HANDLER,
    _clone_native_msg,
    _copy_routing_id,
    _raise_last_error,
    _report_unhandled_callback_exception,
    _request_result_from_errno,
    _routing_id_bytes,
)
from ._socket_base import _enter_callback, _leave_callback
from ._ffi import lib


_ERRNO_ETERM = getattr(errno, "ETERM", 156)
_CALLBACK_SENTINEL = object()


def _timeout_to_ms(timeout):
    if timeout in (None, 0):
        return 0
    return max(1, int(float(timeout) * 1000))


def _payload_parts(payload):
    if isinstance(payload, (list, tuple)):
        parts = list(payload)
    else:
        parts = [payload]
    if not parts:
        raise ValueError("payload must not be empty")
    return parts


def _clone_payload(payload):
    parts = []
    for part in _payload_parts(payload):
        if isinstance(part, Message):
            parts.append(_clone_native_msg(part._msg))
        else:
            copied = Message.copy_from(part)
            parts.append(_clone_native_msg(copied._msg))
            copied.close()
    return parts


def _prepare_native_parts(native_parts):
    parts_array = (ZlinkMsg * len(native_parts))()
    for index, native in enumerate(native_parts):
        parts_array[index] = native
    return parts_array


def _clone_received_owner(parts_ptr, part_count):
    parts_array = (ZlinkMsg * part_count)()
    for index in range(part_count):
        parts_array[index] = _clone_native_msg(parts_ptr[index])
    return _ReceivedPartsOwner(parts_array, part_count)


def _request_received(parts_ptr, part_count, routing_id=None, request_seq=None):
    return Received(
        _clone_received_owner(parts_ptr, int(part_count)),
        routing_id=routing_id,
        request_seq=request_seq,
    )


class _PendingRequest:
    def __init__(self, *, loop=None, callback=None):
        self.loop = loop
        self.future = loop.create_future() if loop is not None else None
        self.callback = callback

    def resolve(self, result, received, errnum=0):
        if self.future is not None:
            if result == RequestResult.OK:
                self.loop.call_soon_threadsafe(self.future.set_result, received)
            else:
                self.loop.call_soon_threadsafe(
                    self.future.set_exception,
                    RequestError(result, errnum),
                )
            return

        if self.callback is None:
            return

        try:
            _enter_callback()
            try:
                self.callback(result, received if result == RequestResult.OK else None)
            finally:
                _leave_callback()
        except Exception:
            _report_unhandled_callback_exception(self.callback)


class RequestDealer:
    def __init__(self, socket):
        self._socket = socket
        self._reply_handler = _REPLY_HANDLER(self._on_reply)
        self._pending = {}

    def request(self, payload, callback=None, *, flags=0, timeout=0):
        if callback is None:
            return self._request_async(payload, flags=flags, timeout=timeout)
        self._request_callback(payload, callback, flags=flags, timeout=timeout)
        return None

    def recv(self, *, flags=0):
        return self._socket.recv(flags=flags)

    def on_receive(self, handler):
        self._socket.on_receive(handler)

    def close(self):
        self._cancel_pending(RequestResult.TERMINATED)
        self._socket.close()

    def _request_async(self, payload, *, flags=0, timeout=0):
        async def _run():
            loop = asyncio.get_running_loop()
            pending = _PendingRequest(loop=loop)
            handle = id(pending)
            self._pending[handle] = pending
            try:
                self._start_request(payload, flags, timeout, handle)
            except Exception:
                self._pending.pop(handle, None)
                raise
            return await pending.future

        return _run()

    def _request_callback(self, payload, callback, *, flags=0, timeout=0):
        pending = _PendingRequest(callback=callback)
        handle = id(pending)
        self._pending[handle] = pending
        try:
            self._start_request(payload, flags, timeout, handle)
        except Exception:
            self._pending.pop(handle, None)
            raise

    def _start_request(self, payload, flags, timeout, handle):
        native_parts = _clone_payload(payload)
        parts_array = _prepare_native_parts(native_parts)
        rc = lib().zlink_dealer_request(
            self._socket._handle,
            parts_array,
            len(native_parts),
            self._reply_handler,
            ctypes.c_void_p(handle),
            int(flags),
            _timeout_to_ms(timeout),
        )
        if rc != 0:
            self._pending.pop(handle, None)
            _raise_last_error()

    def _on_reply(self, errnum, parts, part_count, userdata):
        handle = ctypes.cast(userdata, ctypes.c_void_p).value
        pending = self._pending.pop(handle, None)
        if pending is None:
            return
        result = _request_result_from_errno(int(errnum))
        received = None
        if result == RequestResult.OK:
            received = _request_received(parts, part_count)
        pending.resolve(result, received, int(errnum))

    def _cancel_pending(self, result):
        for handle, pending in list(self._pending.items()):
            self._pending.pop(handle, None)
            if pending.future is not None and not pending.future.done():
                pending.loop.call_soon_threadsafe(
                    pending.future.set_exception,
                    RequestError(result, _ERRNO_ETERM),
                )
            elif pending.callback is not None:
                try:
                    pending.callback(result, None)
                except Exception:
                    _report_unhandled_callback_exception(pending.callback)


class RequestRouter:
    def __init__(self, socket):
        self._socket = socket
        self._data_queue = queue.Queue()
        self._data_handler = None
        self._data_handler_loop = None
        self._reply_handler = _REPLY_HANDLER(self._on_reply)
        self._request_handler = _ROUTER_HANDLER(self._on_request)
        self._pending = {}
        rc = lib().zlink_router_handler(
            self._socket._handle,
            self._request_handler,
            None,
        )
        if rc != 0:
            _raise_last_error()

    def request(self, routing_id, payload, callback=None, *, flags=0, timeout=0):
        if callback is None:
            return self._request_async(routing_id, payload, flags=flags, timeout=timeout)
        self._request_callback(routing_id, payload, callback, flags=flags, timeout=timeout)
        return None

    def reply(self, routing_id, request_seq, payload, *, flags=0):
        native_parts = _clone_payload(payload)
        parts_array = _prepare_native_parts(native_parts)
        native_rid = _copy_routing_id(routing_id)
        rc = lib().zlink_router_reply(
            self._socket._handle,
            ctypes.byref(native_rid),
            ctypes.c_uint64(request_seq),
            parts_array,
            len(native_parts),
        )
        if rc != 0:
            _raise_last_error()

    def recv(self, *, flags=0):
        if int(flags) != 0:
            try:
                item = self._data_queue.get_nowait()
            except queue.Empty as exc:
                raise RecvError(RecvResult.NO_DATA, _ERRNO_ETERM) from exc
        else:
            item = self._data_queue.get()
        if item is None:
            raise ZlinkError(_ERRNO_ETERM, "socket is closed")
        return item

    def on_receive(self, handler):
        self._data_handler = handler
        try:
            self._data_handler_loop = asyncio.get_running_loop()
        except RuntimeError:
            self._data_handler_loop = None

    def close(self):
        self._cancel_pending(RequestResult.TERMINATED)
        self._data_queue.put(None)
        self._socket.close()

    def _request_async(self, routing_id, payload, *, flags=0, timeout=0):
        async def _run():
            loop = asyncio.get_running_loop()
            pending = _PendingRequest(loop=loop)
            handle = id(pending)
            self._pending[handle] = pending
            try:
                self._start_request(routing_id, payload, flags, timeout, handle)
            except Exception:
                self._pending.pop(handle, None)
                raise
            return await pending.future

        return _run()

    def _request_callback(self, routing_id, payload, callback, *, flags=0, timeout=0):
        pending = _PendingRequest(callback=callback)
        handle = id(pending)
        self._pending[handle] = pending
        try:
            self._start_request(routing_id, payload, flags, timeout, handle)
        except Exception:
            self._pending.pop(handle, None)
            raise

    def _start_request(self, routing_id, payload, flags, timeout, handle):
        native_parts = _clone_payload(payload)
        parts_array = _prepare_native_parts(native_parts)
        native_rid = _copy_routing_id(routing_id)
        rc = lib().zlink_router_request(
            self._socket._handle,
            ctypes.byref(native_rid),
            parts_array,
            len(native_parts),
            self._reply_handler,
            ctypes.c_void_p(handle),
            int(flags),
            _timeout_to_ms(timeout),
        )
        if rc != 0:
            self._pending.pop(handle, None)
            _raise_last_error()

    def _on_reply(self, errnum, parts, part_count, userdata):
        handle = ctypes.cast(userdata, ctypes.c_void_p).value
        pending = self._pending.pop(handle, None)
        if pending is None:
            return
        result = _request_result_from_errno(int(errnum))
        received = None
        if result == RequestResult.OK:
            received = _request_received(parts, part_count)
        pending.resolve(result, received, int(errnum))

    def _on_request(self, peer_rid, request_seq, parts, part_count, userdata):
        routing_id = RoutingId(bytes(peer_rid.contents.data[: peer_rid.contents.size]))
        received = _request_received(
            parts,
            part_count,
            routing_id=routing_id,
            request_seq=int(request_seq),
        )
        if self._data_handler is None:
            self._data_queue.put(received)
            return

        def _deliver():
            try:
                _enter_callback()
                try:
                    self._data_handler(received)
                finally:
                    _leave_callback()
            finally:
                received.close()

        if self._data_handler_loop is None:
            _deliver()
        else:
            self._data_handler_loop.call_soon_threadsafe(_deliver)

    def _cancel_pending(self, result):
        for handle, pending in list(self._pending.items()):
            self._pending.pop(handle, None)
            if pending.future is not None and not pending.future.done():
                pending.loop.call_soon_threadsafe(
                    pending.future.set_exception,
                    RequestError(result, _ERRNO_ETERM),
                )
            elif pending.callback is not None:
                try:
                    pending.callback(result, None)
                except Exception:
                    _report_unhandled_callback_exception(pending.callback)


class _ReceivedPartsOwner:
    def __init__(self, parts_ptr, part_count):
        self._parts_ptr = parts_ptr
        self._part_count = part_count
        self._closed = False
        self._open_parts = [True] * part_count

    def msg(self, index):
        if self._closed or not self._open_parts[index]:
            raise RuntimeError("received message is closed")
        return self._parts_ptr[index]

    def close_part(self, index):
        if self._closed or not self._open_parts[index]:
            return
        self._open_parts[index] = False
        if not any(self._open_parts):
            self.close()

    def close(self):
        if self._closed:
            return
        for index in range(self._part_count):
            lib().zlink_msg_close(ctypes.byref(self._parts_ptr[index]))
        self._parts_ptr = None
        self._open_parts = [False] * self._part_count
        self._closed = True
