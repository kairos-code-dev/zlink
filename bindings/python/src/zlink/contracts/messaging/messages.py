# SPDX-License-Identifier: MPL-2.0

import ctypes

from ..enums.enums import CloseResult, ConfigResult, RecvResult, SubmitResult
from ..errors.errors import CloseError, ConfigError, RecvError, SubmitError
from ..._native.ffi import ZlinkMsg, lib
from ..._runtime.core.core import (
    _init_msg_from_buffer,
    _msg_data_ptr,
    _msg_gets,
    _msg_refcnt,
    _msg_size,
    _msg_to_bytes,
    _raise_result_error,
)


# Per-message metadata key range.
METADATA_KEY_USER_MIN = 0x0100
METADATA_VALUE_MAX = 65535


class ReceivedMessage:
    def __init__(self, msg=None, routing_id=None, *, owner=None, index=None):
        self._msg = msg
        self._owner = owner
        self._index = index
        self._closed = False
        self.routing_id = routing_id

    @classmethod
    def _from_owner(cls, owner, index, routing_id=None):
        return cls(routing_id=routing_id, owner=owner, index=index)

    def _native_msg(self):
        if self._owner is not None:
            return self._owner.msg(self._index)
        if self._closed:
            raise RuntimeError("received message is closed")
        return self._msg

    def __len__(self):
        return _msg_size(self._native_msg())

    def to_bytes(self):
        return _msg_to_bytes(self._native_msg())

    def close(self):
        if self._closed:
            return
        self._closed = True
        if self._owner is not None:
            self._owner.close_part(self._index)
            return
        rc = lib().zlink_msg_close(ctypes.byref(self._msg))
        if rc != 0:
            _raise_result_error(CloseError, CloseResult, rc, lib().zlink_errno())

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, tb):
        self.close()

    async def __aenter__(self):
        return self

    async def __aexit__(self, exc_type, exc, tb):
        self.close()


class _BaseReceived:
    """Shared parts/lifecycle for received-message containers.

    ReceivedMultipart and TopicMessage previously carried byte-identical
    copies of the iterator, length, byte-extraction, single-part, and
    context-manager methods; they only diverge in which routing fields
    their __init__/_adopt_from manage. The common surface lives here so
    the two stay in lockstep.
    """

    _owner = None
    parts = ()

    @staticmethod
    def _build_parts(owner):
        return tuple(
            ReceivedMessage._from_owner(owner, index)
            for index in range(owner._part_count)
        )

    def __iter__(self):
        return iter(self.parts)

    def __len__(self):
        return len(self.parts)

    def to_bytes_list(self):
        if self._owner is not None:
            return [
                _msg_to_bytes(self._owner.msg(index))
                for index in range(self._owner._part_count)
            ]
        return [message.to_bytes() for message in self.parts]

    def is_single_part(self):
        return len(self.parts) == 1

    def first_part(self):
        if not self.parts:
            raise RecvError(RecvResult.NO_DATA, 0)
        return self.parts[0]

    def single_part_or_throw(self):
        if len(self.parts) != 1:
            raise RecvError(RecvResult.NO_DATA, 0)
        return self.parts[0]

    def close(self):
        if self._owner is None:
            return
        self._owner.close()
        self._owner = None
        self.parts = ()

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, tb):
        self.close()

    async def __aenter__(self):
        return self

    async def __aexit__(self, exc_type, exc, tb):
        self.close()


class ReceivedMultipart(_BaseReceived):
    def __init__(
        self,
        owner=None,
        routing_id=None,
        request_seq=None,
        *,
        spot_rid=None,
        reply_sender=None,
        send_sender=None,
    ):
        # Caller-provided storage path: ReceivedMultipart() / Received()
        # constructs an empty placeholder for reuse across recv_into calls.
        # See doc/spec/bindings/README.md "Canonical Recv: Caller-Provided
        # Storage". Populated state is installed via _adopt_from().
        if owner is None:
            self._owner = None
            self.parts = ()
            self.routing_id = None
            self.spot_rid = None
            self.request_seq = None
            self._reply_sender = None
            self._send_sender = None
            return
        self._owner = owner
        self.parts = self._build_parts(owner)
        self.routing_id = routing_id
        self.spot_rid = spot_rid
        self.request_seq = request_seq
        self._reply_sender = reply_sender
        self._send_sender = send_sender

    def _adopt_from(self, source):
        """Replace this Received's internal state with the contents of
        ``source``. Closes any state currently held first; ``source`` is
        left detached after the call."""
        if source is self:
            return
        if self._owner is not None:
            try:
                self._owner.close()
            except Exception:  # noqa: BLE001
                pass
        self._owner = source._owner
        self.parts = source.parts
        self.routing_id = source.routing_id
        self.spot_rid = source.spot_rid
        self.request_seq = source.request_seq
        self._reply_sender = source._reply_sender
        self._send_sender = source._send_sender
        source._owner = None
        source.parts = ()
        source.routing_id = None
        source.spot_rid = None
        source.request_seq = None
        source._reply_sender = None
        source._send_sender = None

    def _replace(
        self,
        owner,
        routing_id=None,
        request_seq=None,
        *,
        spot_rid=None,
        reply_sender=None,
        send_sender=None,
    ):
        if self._owner is not None:
            try:
                self._owner.close()
            except Exception:  # noqa: BLE001
                pass
        self._owner = owner
        self.parts = self._build_parts(owner)
        self.routing_id = routing_id
        self.spot_rid = spot_rid
        self.request_seq = request_seq
        self._reply_sender = reply_sender
        self._send_sender = send_sender


class TopicMessage(_BaseReceived):
    def __init__(
        self,
        topic=None,
        owner=None,
        routing_id=None,
        request_seq=None,
    ):
        if owner is None:
            self._topic = ""
            self._topic_raw = None
            self._owner = None
            self.parts = ()
            self.routing_id = None
            self.request_seq = None
            return
        self._topic = topic
        self._topic_raw = None
        self._owner = owner
        self.parts = self._build_parts(owner)
        self.routing_id = routing_id
        self.request_seq = request_seq

    @property
    def topic(self):
        raw = self._topic_raw
        if raw is not None:
            self._topic = raw.decode("utf-8", errors="replace")
            self._topic_raw = None
        return self._topic

    @topic.setter
    def topic(self, value):
        self._topic = value
        self._topic_raw = None

    def _adopt_from(self, source):
        if source is self:
            return
        if self._owner is not None:
            try:
                self._owner.close()
            except Exception:  # noqa: BLE001
                pass
        self._topic = source._topic
        self._topic_raw = source._topic_raw
        self._owner = source._owner
        self.parts = source.parts
        self.routing_id = source.routing_id
        self.request_seq = source.request_seq
        source._topic = ""
        source._topic_raw = None
        source._owner = None
        source.parts = ()
        source.routing_id = None
        source.request_seq = None

    def _replace(
        self,
        owner,
        *,
        topic="",
        topic_raw=None,
        routing_id=None,
        request_seq=None,
    ):
        if self._owner is not None:
            try:
                self._owner.close()
            except Exception:  # noqa: BLE001
                pass
        self._topic = topic
        self._topic_raw = topic_raw
        self._owner = owner
        self.parts = self._build_parts(owner)
        self.routing_id = routing_id
        self.request_seq = request_seq


class Received(ReceivedMultipart):
    def send(self):
        """Return a SendOp routed back to the source of this Received.

        Source rid / spot rid are encapsulated; accumulate payload via
        ``.message(...)`` before calling ``.submit()``.
        """
        if self._send_sender is None:
            raise SubmitError(SubmitResult.INVALID_STATE, 0)
        return self._send_sender()

    def reply(self):
        """Return a ReplyOp for this received request. Valid only when
        ``request_seq`` is present."""
        if self.request_seq is None or self._reply_sender is None:
            raise SubmitError(SubmitResult.INVALID_STATE, 0)
        return self._reply_sender()


class SubscriptionEvent:
    def __init__(self, topic="", subscribed=False, routing_id=None):
        self.routing_id = routing_id
        self.topic = topic
        self.subscribed = subscribed

    def _adopt_from(self, source):
        if source is self:
            return
        self.routing_id = source.routing_id
        self.topic = source.topic
        self.subscribed = source.subscribed
        source.routing_id = None
        source.topic = ""
        source.subscribed = False


class Message:
    def __init__(self, size: int | None = None):
        self._msg = ZlinkMsg()
        self._valid = False
        self._keepalive = None
        if size is None:
            rc = lib().zlink_msg_init(ctypes.byref(self._msg))
        else:
            rc = lib().zlink_msg_init_size(ctypes.byref(self._msg), size)
        if rc != 0:
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())
        self._valid = True

    @classmethod
    def copy_from(cls, data):
        msg = cls.__new__(cls)
        msg._msg = ZlinkMsg()
        msg._valid = False
        msg._keepalive = _init_msg_from_buffer(msg._msg, data, borrow=False)
        msg._valid = True
        return msg

    @classmethod
    def _wrap_buffer(cls, data):
        msg = cls.__new__(cls)
        msg._msg = ZlinkMsg()
        msg._valid = False
        msg._keepalive = _init_msg_from_buffer(msg._msg, data, borrow=True)
        msg._valid = True
        return msg

    @staticmethod
    def from_bytes(data: bytes):
        return Message.copy_from(data)

    def size(self):
        return _msg_size(self._msg) if self._valid else 0

    @property
    def data(self):
        if not self._valid:
            return memoryview(b"")
        # Cache the memoryview keyed by (ptr, size). The underlying msg can
        # only be mutated by close()/_adopt_from()-style transitions, both of
        # which clear `_valid` and therefore invalidate this cache via the
        # ``not self._valid`` short-circuit above. Reading `.data` repeatedly
        # — common when forwarding a payload between parts of a pipeline —
        # would otherwise allocate a fresh `from_address` view every call.
        ptr = _msg_data_ptr(self._msg)
        size = self.size()
        if not ptr or size <= 0:
            return memoryview(b"")
        cache = getattr(self, "_data_view_cache", None)
        if cache is not None and cache[0] == ptr and cache[1] == size:
            return cache[2]
        view = memoryview((ctypes.c_ubyte * size).from_address(ptr))
        self._data_view_cache = (ptr, size, view)
        return view

    def to_bytes(self):
        return _msg_to_bytes(self._msg) if self._valid else b""

    def get_property(self, name):
        return _msg_gets(self._msg, name) if self._valid else None

    def getProperty(self, name):
        return self.get_property(name)

    def ref_count(self):
        return _msg_refcnt(self._msg) if self._valid else -1

    def refCount(self):
        return self.ref_count()

    def close(self):
        if not self._valid:
            return
        rc = lib().zlink_msg_close(ctypes.byref(self._msg))
        self._valid = False
        self._keepalive = None
        self._data_view_cache = None
        if rc != 0:
            _raise_result_error(CloseError, CloseResult, rc, lib().zlink_errno())

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, tb):
        self.close()

    async def __aenter__(self):
        return self

    async def __aexit__(self, exc_type, exc, tb):
        self.close()
