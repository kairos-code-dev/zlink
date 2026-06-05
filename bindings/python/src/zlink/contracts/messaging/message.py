# SPDX-License-Identifier: MPL-2.0

from typing import Protocol, runtime_checkable

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


def _register_messaging_factories(
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


def create_message(size: int | None = None):
    """Create a message: empty when ``size`` is ``None``, otherwise with
    writable payload storage of ``size`` bytes."""
    return _require(_message_factory, "message")(size)


def allocate_message(size: int):
    """Allocate a message with writable payload storage of ``size`` bytes."""
    return _require(_message_allocate_factory, "message allocate")(size)


def _message_from(data):
    """Create a message holding an independent copy of ``data``."""
    return _require(_message_from_factory, "message from")(data)


def _wrap_message_buffer(data):
    """Create a message that wraps ``data`` without copying it.

    The caller must keep ``data`` alive and unmodified for as long as the
    message is in use.
    """
    return _require(_message_wrap_buffer_factory, "message buffer")(data)


@runtime_checkable
class Message(Protocol):
    """A message payload.

    Supports the context-manager protocol; closing the message (or leaving its
    ``with`` block) releases the payload. Sending a message consumes it.
    """

    @classmethod
    def from_(cls, data):
        """Create a message holding an independent copy of ``data``."""
        return _message_from(data)

    @classmethod
    def allocate(cls, size: int):
        """Allocate a message with writable payload storage of ``size`` bytes."""
        return allocate_message(size)

    def copy(self):
        """Return a new message holding an independent copy of this payload."""
        ...

    def size(self):
        """Return the payload size in bytes."""
        ...

    def is_empty(self):
        """Return ``True`` when the payload is empty."""
        ...

    @property
    def data(self):
        """A zero-copy ``memoryview`` of the payload, valid only while the
        message is open."""
        ...

    def to_bytes(self):
        """Return a new ``bytes`` object copying the payload."""
        ...

    def copy_to(self, destination, source_offset=0, destination_offset=0, length=None):
        """Copy the payload (or the ``length`` bytes from ``source_offset``)
        into ``destination`` at ``destination_offset``; return the number of
        bytes written."""
        ...

    def try_copy_to(self, destination):
        """Copy the payload into ``destination`` when it fits; return the number
        of bytes written, or ``None`` when ``destination`` is too small."""
        ...

    def to_string(self, encoding="utf-8"):
        """Decode the payload as text using ``encoding``."""
        ...

    def get_property(self, name):
        """Return the native message property ``name``, or ``None`` when it is
        absent."""
        ...

    def ref_count(self):
        """Return the native payload reference count (a diagnostic only; it does
        not affect ownership)."""
        ...

    def close(self):
        """Release the payload storage owned by this message."""
        ...

    def __enter__(self):
        """Enter the context manager, returning this message."""
        ...

    def __exit__(self, exc_type, exc, tb):
        """Close the message on leaving the ``with`` block."""
        ...

    async def __aenter__(self):
        """Enter the async context manager, returning this message."""
        ...

    async def __aexit__(self, exc_type, exc, tb):
        """Close the message on leaving the ``async with`` block."""
        ...


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
