# SPDX-License-Identifier: MPL-2.0

from ..errors.errors import RecvError, SubmitError
from ..sockets.codes import RecvResult, SubmitResult


METADATA_KEY_USER_MIN = 0x0100
METADATA_VALUE_MAX = 65535

_message_factory = None
_message_from_factory = None
_message_allocate_factory = None
_message_wrap_buffer_factory = None
_received_message_factory = None
_received_message_from_owner_factory = None
_received_multipart_factory = None
_received_factory = None
_topic_message_factory = None
_subscription_event_factory = None


def register_messaging_factories(
    *,
    message_factory,
    message_from_factory,
    message_allocate_factory,
    message_wrap_buffer_factory,
    received_message_factory,
    received_message_from_owner_factory,
    received_multipart_factory,
    received_factory,
    topic_message_factory,
    subscription_event_factory,
):
    global _message_factory
    global _message_from_factory
    global _message_allocate_factory
    global _message_wrap_buffer_factory
    global _received_message_factory
    global _received_message_from_owner_factory
    global _received_multipart_factory
    global _received_factory
    global _topic_message_factory
    global _subscription_event_factory
    _message_factory = message_factory
    _message_from_factory = message_from_factory
    _message_allocate_factory = message_allocate_factory
    _message_wrap_buffer_factory = message_wrap_buffer_factory
    _received_message_factory = received_message_factory
    _received_message_from_owner_factory = received_message_from_owner_factory
    _received_multipart_factory = received_multipart_factory
    _received_factory = received_factory
    _topic_message_factory = topic_message_factory
    _subscription_event_factory = subscription_event_factory


def _require(factory, name):
    if factory is None:
        raise RuntimeError(f"zlink {name} runtime is not registered")
    return factory


class ReceivedMessage:
    def __new__(cls, *args, **kwargs):
        if cls is ReceivedMessage:
            return _require(_received_message_factory, "received message")(
                *args, **kwargs
            )
        return super().__new__(cls)

    @classmethod
    def _from_owner(cls, owner, index, routing_id=None):
        if cls is ReceivedMessage:
            return _require(
                _received_message_from_owner_factory, "received message"
            )(owner, index, routing_id)
        return cls(routing_id=routing_id, owner=owner, index=index)

    def __len__(self): ...

    @property
    def data(self): ...

    def to_bytes(self): ...

    def close(self): ...

    def __enter__(self): ...

    def __exit__(self, exc_type, exc, tb): ...

    async def __aenter__(self): ...

    async def __aexit__(self, exc_type, exc, tb): ...


class _BaseReceived:
    _owner = None
    parts = ()

    def __iter__(self):
        return iter(self.parts)

    def __len__(self):
        return len(self.parts)

    def to_bytes_list(self):
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
        if self._owner is not None:
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
    def __new__(cls, *args, **kwargs):
        if cls is ReceivedMultipart:
            return _require(_received_multipart_factory, "received multipart")(
                *args, **kwargs
            )
        return super().__new__(cls)

    def _adopt_from(self, source): ...

    def _replace(self, owner, routing_id=None, request_seq=None, **kwargs): ...


class TopicMessage(_BaseReceived):
    def __new__(cls, *args, **kwargs):
        if cls is TopicMessage:
            return _require(_topic_message_factory, "topic message")(
                *args, **kwargs
            )
        return super().__new__(cls)

    @property
    def topic(self): ...

    @topic.setter
    def topic(self, value): ...

    def _adopt_from(self, source): ...

    def _replace(self, owner, **kwargs): ...


class Received(ReceivedMultipart):
    def __new__(cls, *args, **kwargs):
        if cls is Received:
            return _require(_received_factory, "received")(*args, **kwargs)
        return super().__new__(cls)

    def send(self):
        if self._send_sender is None:
            raise SubmitError(SubmitResult.INVALID_STATE, 0)
        return self._send_sender()

    def reply(self):
        if self.request_seq is None or self._reply_sender is None:
            raise SubmitError(SubmitResult.INVALID_STATE, 0)
        return self._reply_sender()


class SubscriptionEvent:
    def __new__(cls, *args, **kwargs):
        if cls is SubscriptionEvent:
            return _require(_subscription_event_factory, "subscription event")(
                *args, **kwargs
            )
        return super().__new__(cls)

    def _adopt_from(self, source): ...


class Message:
    def __new__(cls, size: int | None = None):
        if cls is Message:
            return _require(_message_factory, "message")(size)
        return super().__new__(cls)

    @classmethod
    def allocate(cls, size: int):
        if cls is Message:
            return _require(_message_allocate_factory, "message allocate")(size)
        return cls(size)

    @classmethod
    def from_(cls, data):
        if cls is Message:
            return _require(_message_from_factory, "message from")(data)
        raise NotImplementedError

    @classmethod
    def _wrap_buffer(cls, data):
        if cls is Message:
            return _require(_message_wrap_buffer_factory, "message buffer")(data)
        raise NotImplementedError

    def copy(self): ...

    def size(self): ...

    def is_empty(self): ...

    @property
    def data(self): ...

    def to_bytes(self): ...

    def copy_to(self, destination, source_offset=0, destination_offset=0, length=None): ...

    def try_copy_to(self, destination): ...

    def to_string(self, encoding="utf-8"): ...

    def get_property(self, name): ...

    def getProperty(self, name): ...

    def ref_count(self): ...

    def refCount(self): ...

    def close(self): ...

    def __enter__(self): ...

    def __exit__(self, exc_type, exc, tb): ...

    async def __aenter__(self): ...

    async def __aexit__(self, exc_type, exc, tb): ...
