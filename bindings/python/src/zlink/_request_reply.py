# SPDX-License-Identifier: MPL-2.0

import asyncio
import ctypes
import errno
import queue
import threading

from ._core import MSG_TYPE_REPLY, MSG_TYPE_REQUEST, Message, ReceivedMessage, ZlinkError
from ._enums import SendResult
from ._ffi import lib


def _request_info(message):
    if isinstance(message, ReceivedMessage):
        native = message._native_msg()
        msg_ref = ctypes.byref(native)
    elif isinstance(message, Message):
        msg_ref = ctypes.byref(message._msg)
    else:
        raise TypeError("message must be Message or ReceivedMessage")
    msg_type = ctypes.c_uint8(0)
    correlation_id = ctypes.c_uint64(0)
    rc = lib().zlink_msg_get_request_info(
        msg_ref, ctypes.byref(msg_type), ctypes.byref(correlation_id)
    )
    if rc != 0:
        raise ZlinkError(errno.EINVAL, "failed to inspect request info")
    return msg_type.value, correlation_id.value


class _BufferedReceived:
    def __init__(self, parts, routing_id):
        self.parts = tuple(parts)
        self.routing_id = routing_id

    def to_bytes_list(self):
        return [part.to_bytes() for part in self.parts]

    def close(self):
        for part in self.parts:
            part.close()

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, tb):
        self.close()


def _clone_received(received):
    parts = [_clone_message(part) for part in received.parts]
    return _BufferedReceived(parts, received.routing_id)


def _clone_message(message):
    cloned = Message.copy_from(message.to_bytes())
    msg_type, correlation_id = _request_info(message)
    if msg_type == MSG_TYPE_REQUEST:
        cloned.set_request(correlation_id)
    elif msg_type == MSG_TYPE_REPLY:
        cloned.set_reply(correlation_id)
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


class _PendingRequest:
    def __init__(self, loop, on_reply=None, on_error=None):
        self.loop = loop
        self.future = loop.create_future() if on_reply is None else None
        self.on_reply = on_reply
        self.on_error = on_error
        self.done = False
        self.lock = threading.Lock()

    def resolve(self, received):
        with self.lock:
            if self.done:
                received.close()
                return
            self.done = True
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
        with self.lock:
            if self.done:
                return
            self.done = True
        if self.future is not None:
            self.loop.call_soon_threadsafe(self.future.set_exception, exc)
            return
        if self.on_error is not None:
            self.loop.call_soon_threadsafe(self.on_error, exc)


class _RequestReplyBase:
    _RECV_POLL_INTERVAL = 0.001

    def __init__(self, socket):
        self._socket = socket
        self._dispatch_started = False
        self._dispatch_lock = threading.Lock()
        self._data_queue = queue.Queue()
        self._data_handler = None
        self._data_handler_loop = None
        self._pending_lock = threading.Lock()
        self._pending = {}
        self._next_correlation_id = 1
        self._closed = False
        self._dispatch_thread = None

    def _ensure_dispatch_started(self):
        if self._dispatch_started:
            return
        with self._dispatch_lock:
            if self._dispatch_started:
                return
            self._dispatch_thread = threading.Thread(
                target=self._dispatch_loop,
                name="zlink-request-reply",
                daemon=True,
            )
            self._dispatch_thread.start()
            self._dispatch_started = True

    def _next_pending(self, on_reply=None, on_error=None):
        loop = asyncio.get_running_loop()
        with self._pending_lock:
            correlation_id = self._next_correlation_id
            self._next_correlation_id += 1
            pending = _PendingRequest(loop, on_reply=on_reply, on_error=on_error)
            self._pending[correlation_id] = pending
        return correlation_id, pending

    def _pop_pending(self, correlation_id):
        with self._pending_lock:
            return self._pending.pop(correlation_id, None)

    def _fail_pending(self, correlation_id, exc):
        pending = self._pop_pending(correlation_id)
        if pending is not None:
            pending.reject(exc)

    def _dispatch_loop(self):
        while not self._closed:
            try:
                received = self._socket.try_recv()
            except Exception as exc:
                if self._closed:
                    break
                with self._pending_lock:
                    pending = list(self._pending.values())
                    self._pending.clear()
                for item in pending:
                    item.reject(exc)
                self._data_queue.put(None)
                break
            if received is None:
                threading.Event().wait(self._RECV_POLL_INTERVAL)
                continue
            try:
                cloned = _clone_received(received)
            finally:
                received.close()
            first = cloned.parts[0] if cloned.parts else None
            if first is None:
                self._deliver_data(cloned)
                continue
            msg_type, correlation_id = _request_info(first)
            if msg_type == MSG_TYPE_REPLY:
                pending = self._pop_pending(correlation_id)
                if pending is None:
                    cloned.close()
                    continue
                pending.resolve(cloned)
                continue
            if msg_type == MSG_TYPE_REQUEST and self._handle_request(cloned, correlation_id):
                continue
            self._deliver_data(cloned)

    def _deliver_data(self, received):
        if self._data_handler is None:
            self._data_queue.put(received)
            return

        def _deliver():
            try:
                self._data_handler(received)
            finally:
                received.close()

        loop = self._data_handler_loop
        if loop is None:
            _deliver()
        else:
            loop.call_soon_threadsafe(_deliver)

    def recv(self):
        self._ensure_dispatch_started()
        item = self._data_queue.get()
        if item is None:
            raise ZlinkError(errno.ETERM, "socket is closed")
        return item

    def try_recv(self):
        self._ensure_dispatch_started()
        try:
            item = self._data_queue.get_nowait()
        except queue.Empty:
            return None
        if item is None:
            raise ZlinkError(errno.ETERM, "socket is closed")
        return item

    def on_receive(self, handler):
        if handler is None:
            raise ValueError("handler must not be None")
        self._ensure_dispatch_started()
        self._data_handler = handler
        try:
            self._data_handler_loop = asyncio.get_running_loop()
        except RuntimeError:
            self._data_handler_loop = None

    def close(self):
        if self._closed:
            return
        self._closed = True
        with self._pending_lock:
            pending = list(self._pending.values())
            self._pending.clear()
        for item in pending:
            item.reject(ZlinkError(errno.ETERM, "socket is closed"))
        self._data_queue.put(None)
        self._socket.close()

    def _handle_request(self, received, correlation_id):
        received.close()
        return False


class RequestDealer(_RequestReplyBase):
    def __init__(self, socket):
        super().__init__(socket)

    async def request(self, payload, timeout=None):
        self._ensure_dispatch_started()
        correlation_id, pending = self._next_pending()
        parts = _clone_payload(payload)
        parts[0].set_request(correlation_id)
        try:
            self._socket.send(parts)
        except Exception as exc:
            self._fail_pending(correlation_id, exc)
            raise
        if timeout is None:
            return await pending.future
        return await asyncio.wait_for(pending.future, timeout)

    async def try_request(self, payload, timeout=None):
        self._ensure_dispatch_started()
        correlation_id, pending = self._next_pending()
        parts = _clone_payload(payload)
        parts[0].set_request(correlation_id)
        try:
            result = self._socket.try_send(parts)
        except Exception as exc:
            self._fail_pending(correlation_id, exc)
            raise
        if result is not SendResult.SENT:
            exc = ZlinkError(errno.EAGAIN, "request send is backpressured")
            self._fail_pending(correlation_id, exc)
            raise exc
        if timeout is None:
            return await pending.future
        return await asyncio.wait_for(pending.future, timeout)

    def request_callback(self, payload, on_reply, on_error, timeout=None):
        self._ensure_dispatch_started()
        correlation_id, pending = self._next_pending(
            on_reply=on_reply,
            on_error=on_error,
        )
        parts = _clone_payload(payload)
        parts[0].set_request(correlation_id)
        try:
            self._socket.send(parts)
        except Exception as exc:
            self._fail_pending(correlation_id, exc)
            if on_error is not None:
                pending.reject(exc)
            return
        if timeout is not None:
            loop = pending.loop

            def _timeout():
                self._fail_pending(correlation_id, ZlinkError(errno.ETIMEDOUT, "request timed out"))

            loop.call_later(timeout, _timeout)


class RequestRouter(_RequestReplyBase):
    def __init__(self, socket):
        super().__init__(socket)
        self._request_handler = None
        self._request_handler_loop = None

    def on_request(self, handler):
        if handler is None:
            raise ValueError("handler must not be None")
        self._ensure_dispatch_started()
        self._request_handler = handler
        try:
            self._request_handler_loop = asyncio.get_running_loop()
        except RuntimeError:
            self._request_handler_loop = None

    def _handle_request(self, received, correlation_id):
        if self._request_handler is None:
            return False

        def _deliver():
            result = self._request_handler(received.routing_id, correlation_id, received)
            if asyncio.iscoroutine(result):
                task = asyncio.create_task(result)
                task.add_done_callback(lambda _: received.close())
            else:
                received.close()

        loop = self._request_handler_loop
        if loop is None:
            _deliver()
        else:
            loop.call_soon_threadsafe(_deliver)
        return True

    async def request(self, routing_id, payload, timeout=None):
        self._ensure_dispatch_started()
        correlation_id, pending = self._next_pending()
        parts = _clone_payload(payload)
        parts[0].set_request(correlation_id)
        try:
            self._socket.send(parts, routing_id=routing_id)
        except Exception as exc:
            self._fail_pending(correlation_id, exc)
            raise
        if timeout is None:
            return await pending.future
        return await asyncio.wait_for(pending.future, timeout)

    async def try_request(self, routing_id, payload, timeout=None):
        self._ensure_dispatch_started()
        correlation_id, pending = self._next_pending()
        parts = _clone_payload(payload)
        parts[0].set_request(correlation_id)
        try:
            result = self._socket.try_send(parts, routing_id=routing_id)
        except Exception as exc:
            self._fail_pending(correlation_id, exc)
            raise
        if result is not SendResult.SENT:
            exc = ZlinkError(errno.EAGAIN, "request send is backpressured")
            self._fail_pending(correlation_id, exc)
            raise exc
        if timeout is None:
            return await pending.future
        return await asyncio.wait_for(pending.future, timeout)

    def request_callback(self, routing_id, payload, on_reply, on_error, timeout=None):
        self._ensure_dispatch_started()
        correlation_id, pending = self._next_pending(
            on_reply=on_reply,
            on_error=on_error,
        )
        parts = _clone_payload(payload)
        parts[0].set_request(correlation_id)
        try:
            self._socket.send(parts, routing_id=routing_id)
        except Exception as exc:
            self._fail_pending(correlation_id, exc)
            if on_error is not None:
                pending.reject(exc)
            return
        if timeout is not None:
            loop = pending.loop

            def _timeout():
                self._fail_pending(correlation_id, ZlinkError(errno.ETIMEDOUT, "request timed out"))

            loop.call_later(timeout, _timeout)

    async def reply(self, routing_id, correlation_id, payload):
        parts = _clone_payload(payload)
        parts[0].set_reply(correlation_id)
        self._socket.send(parts, routing_id=routing_id)

    def try_reply(self, routing_id, correlation_id, payload):
        parts = _clone_payload(payload)
        parts[0].set_reply(correlation_id)
        return self._socket.try_send(parts, routing_id=routing_id)
