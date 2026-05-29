# SPDX-License-Identifier: MPL-2.0

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

    def ref_count(self): ...

    def close(self): ...

    def __enter__(self): ...

    def __exit__(self, exc_type, exc, tb): ...

    async def __aenter__(self): ...

    async def __aexit__(self, exc_type, exc, tb): ...


def __getattr__(name):
    if name in {"Received", "ReceivedMessage", "ReceivedMultipart"}:
        from . import received

        value = getattr(received, name)
    elif name == "TopicMessage":
        from .topic_message import TopicMessage as value
    elif name == "SubscriptionEvent":
        from .subscription_event import SubscriptionEvent as value
    else:
        raise AttributeError(f"module {__name__!r} has no attribute {name!r}")
    globals()[name] = value
    return value
