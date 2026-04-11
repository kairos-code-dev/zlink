# SPDX-License-Identifier: MPL-2.0

import asyncio
import ctypes
import errno
import queue

from ._core import (
    Message,
    ReceivedMessage,
    RoutingId,
    ZlinkError,
    ZlinkMsg,
    _REPLY_HANDLER,
    _ROUTER_HANDLER,
    _clone_native_msg,
    _copy_routing_id,
    _raise_last_error,
)
from ._enums import SendResult
from ._ffi import lib

_ERRNO_ETERM = getattr(errno, "ETERM", 156)


class _BufferedReceived:
    def __init__(self, parts, routing_id=None, request_seq=None):
        self.parts = tuple(parts)
        self.routing_id = routing_id
        self.request_seq = request_seq

    def to_bytes_list(self):
        return [part.to_bytes() for part in self.parts]

    def close(self):
        for part in self.parts:
            part.close()

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, tb):
        self.close()


def _clone_message(message):
    if isinstance(message, ReceivedMessage):
        native = _clone_native_msg(message._native_msg())
    elif isinstance(message, Message):
        native = _clone_native_msg(message._msg)
    else:
        raise TypeError("message must be Message or ReceivedMessage")
    cloned = Message.__new__(Message)
    cloned._msg = native
    cloned._valid = True
    cloned._keepalive = None
    return cloned


def _clone_payload(payload):
    if isinstance(payload, Message):
        parts = [_clone_message(payload)]
    elif isinstance(payload, (list, tuple)):
        parts = []
        for part in payload:
            if isinstance(part, Message):
                parts.append(_clone_message(part))
            else:
                parts.append(Message.copy_from(part))
    else:
        parts = [Message.copy_from(payload)]
    if not parts:
        raise ValueError("payload must not be empty")
    return parts


def _take_callback_parts(parts_ptr, part_count):
    parts = []
    for index in range(int(part_count)):
        native = _clone_native_msg(parts_ptr[index])
        msg = Message.__new__(Message)
        msg._msg = native
        msg._valid = True
        msg._keepalive = None
        parts.append(msg)
    return parts


def _prepare_native_parts(parts):
    native_parts = (ZlinkMsg * len(parts))()
    for index, part in enumerate(parts):
        rc = lib().zlink_msg_init(ctypes.byref(native_parts[index]))
        if rc != 0:
            _raise_last_error()
        rc = lib().zlink_msg_move(ctypes.byref(native_parts[index]), ctypes.byref(part._msg))
        if rc != 0:
            _raise_last_error()
        part._valid = False
        part._keepalive = None
    return native_parts


class _PendingRequest:
    def __init__(self, loop, on_reply=None, on_error=None):
        self.loop = loop
        self.future = loop.create_future() if on_reply is None else None
        self.on_reply = on_reply
        self.on_error = on_error

    def resolve(self, received):
        if self.future is not None:
            self.loop.call_soon_threadsafe(self.future.set_result, received)
            return

        def _deliver():
            try:
                self.on_reply(received)
            finally:
                received.close()

        self.loop.call_soon_threadsafe(_deliver)

    def reject(self, exc):
        if self.future is not None:
            self.loop.call_soon_threadsafe(self.future.set_exception, exc)
            return
        if self.on_error is not None:
            self.loop.call_soon_threadsafe(self.on_error, exc)


class RequestDealer:
    def __init__(self, socket):
        self._socket = socket
        self._reply_handler = _REPLY_HANDLER(self._on_reply)
        self._pending = {}

    async def request(self, payload, timeout=None):
        loop = asyncio.get_running_loop()
        pending = _PendingRequest(loop)
        handle = id(pending)
        self._pending[handle] = pending
        self._start_request(payload, timeout, handle, None)
        return await pending.future

    async def try_request(self, payload, timeout=None):
        return await self.request(payload, timeout=timeout)

    def request_callback(self, payload, on_reply, on_error, timeout=None):
        loop = asyncio.get_running_loop()
        pending = _PendingRequest(loop, on_reply=on_reply, on_error=on_error)
        handle = id(pending)
        self._pending[handle] = pending
        self._start_request(payload, timeout, handle, None)

    def recv(self):
        return self._socket.recv()

    def try_recv(self):
        return self._socket.try_recv()

    def on_receive(self, handler):
        self._socket.on_receive(handler)

    def close(self):
        self._socket.close()

    def _start_request(self, payload, timeout, handle, routing_id):
        parts = _clone_payload(payload)
        native_parts = _prepare_native_parts(parts)
        timeout_ms = _timeout_to_ms(timeout)
        rc = lib().zlink_dealer_request(
            self._socket._handle,
            native_parts,
            len(parts),
            self._reply_handler,
            ctypes.c_void_p(handle),
            0,
            timeout_ms,
        )
        if rc != 0:
            self._pending.pop(handle, None)
            _raise_last_error()

    def _on_reply(self, errnum, parts, part_count, userdata):
        handle = ctypes.cast(userdata, ctypes.c_void_p).value
        pending = self._pending.pop(handle, None)
        if pending is None:
            return
        if errnum != 0:
            pending.reject(ZlinkError(errnum, os_error_message(errnum)))
            return
        pending.resolve(_BufferedReceived(_take_callback_parts(parts, part_count)))


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

    async def request(self, routing_id, payload, timeout=None):
        loop = asyncio.get_running_loop()
        pending = _PendingRequest(loop)
        handle = id(pending)
        self._pending[handle] = pending
        self._start_request(routing_id, payload, timeout, handle)
        return await pending.future

    async def try_request(self, routing_id, payload, timeout=None):
        return await self.request(routing_id, payload, timeout=timeout)

    def request_callback(self, routing_id, payload, on_reply, on_error, timeout=None):
        loop = asyncio.get_running_loop()
        pending = _PendingRequest(loop, on_reply=on_reply, on_error=on_error)
        handle = id(pending)
        self._pending[handle] = pending
        self._start_request(routing_id, payload, timeout, handle)

    async def reply(self, routing_id, request_seq, payload):
        parts = _clone_payload(payload)
        native_parts = _prepare_native_parts(parts)
        native_rid = _copy_routing_id(routing_id)
        rc = lib().zlink_router_reply(
            self._socket._handle,
            ctypes.byref(native_rid),
            ctypes.c_uint64(request_seq),
            native_parts,
            len(parts),
        )
        if rc != 0:
            _raise_last_error()

    def try_reply(self, routing_id, request_seq, payload):
        loop = asyncio.new_event_loop()
        try:
            loop.run_until_complete(self.reply(routing_id, request_seq, payload))
        finally:
            loop.close()
        return SendResult.SENT

    def recv(self):
        item = self._data_queue.get()
        if item is None:
            raise ZlinkError(_ERRNO_ETERM, "socket is closed")
        return item

    def try_recv(self):
        try:
            item = self._data_queue.get_nowait()
        except queue.Empty:
            return None
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
        self._data_queue.put(None)
        self._socket.close()

    def _start_request(self, routing_id, payload, timeout, handle):
        parts = _clone_payload(payload)
        native_parts = _prepare_native_parts(parts)
        native_rid = _copy_routing_id(routing_id)
        rc = lib().zlink_router_request(
            self._socket._handle,
            ctypes.byref(native_rid),
            native_parts,
            len(parts),
            self._reply_handler,
            ctypes.c_void_p(handle),
            0,
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
        if errnum != 0:
            pending.reject(ZlinkError(errnum, os_error_message(errnum)))
            return
        pending.resolve(_BufferedReceived(_take_callback_parts(parts, part_count)))

    def _on_request(self, peer_rid, request_seq, parts, part_count, userdata):
        routing_id = RoutingId(bytes(peer_rid.contents.data[: peer_rid.contents.size]))
        received = _BufferedReceived(
            _take_callback_parts(parts, part_count),
            routing_id=routing_id,
            request_seq=int(request_seq),
        )
        if self._data_handler is None:
            self._data_queue.put(received)
            return

        def _deliver():
            try:
                self._data_handler(received)
            finally:
                received.close()

        if self._data_handler_loop is None:
            _deliver()
        else:
            self._data_handler_loop.call_soon_threadsafe(_deliver)


def _timeout_to_ms(timeout):
    if timeout is None:
        return 0
    return max(1, int(float(timeout) * 1000))


def os_error_message(errnum):
    try:
        return errno.errorcode.get(int(errnum), "zlink error")
    except Exception:
        return "zlink error"
