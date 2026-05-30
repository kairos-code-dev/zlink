# SPDX-License-Identifier: MPL-2.0

from dataclasses import dataclass
from typing import Protocol, runtime_checkable

from .codes import PollSourceKind

_poller_factory = None
_poll_events_factory = None


def register_poller_factories(*, poller_factory, poll_events_factory):
    global _poller_factory
    global _poll_events_factory
    _poller_factory = poller_factory
    _poll_events_factory = poll_events_factory


@dataclass(frozen=True)
class PollEvent:
    """One ready source reported by :meth:`Poller.wait`.

    Attributes:
        source_kind: Whether the ready source is a socket, file descriptor, or timer.
        slot: The caller token supplied when the source was registered.
        revents: The events that fired, as a mask of poll flags.
        fd: The file descriptor, when the source is a raw fd.
    """

    source_kind: PollSourceKind
    slot: int
    revents: int
    fd: int = 0


def create_poll_events(capacity):
    """Create a reusable result buffer holding up to ``capacity`` poll
    events."""
    if _poll_events_factory is None:
        raise RuntimeError("zlink poll events runtime is not registered")
    return _poll_events_factory(capacity)


@runtime_checkable
class PollEvents(Protocol):
    """A reusable buffer of poll results filled by :meth:`Poller.wait`."""

    @property
    def capacity(self):
        """The maximum number of results the buffer can hold."""
        ...

    @property
    def ready_count(self):
        """The number of ready sources written by the last wait."""
        ...

    def source_kind(self, index):
        """Return the source kind of the result at ``index``."""
        ...

    def slot(self, index):
        """Return the caller token of the result at ``index``."""
        ...

    def revents(self, index):
        """Return the fired-event mask of the result at ``index``."""
        ...

    def has_event(self, index, event):
        """Return whether the result at ``index`` includes ``event``."""
        ...

    def fd(self, index):
        """Return the file descriptor of the result at ``index``."""
        ...

    def event(self, index):
        """Return the result at ``index`` as a :class:`PollEvent`."""
        ...


def create_poller():
    """Create an empty poller. The caller owns it."""
    if _poller_factory is None:
        raise RuntimeError("zlink poller runtime is not registered")
    return _poller_factory()


@runtime_checkable
class Poller(Protocol):
    """Multiplexes sockets, file descriptors, and timers, reporting which become
    ready on a single wait."""

    def add_socket(self, socket, events, slot):
        """Register ``socket`` to be watched for ``events``; ``slot`` is echoed
        back in the matching result."""
        ...

    def add_fd(self, fd, events, slot):
        """Register raw file descriptor ``fd`` to be watched for ``events``;
        ``slot`` is echoed back in the matching result."""
        ...

    def add_timer(self, timer, slot):
        """Register ``timer``; its expirations surface as poll events tagged
        with ``slot``."""
        ...

    def modify_socket(self, socket, events):
        """Change the watched events for an already-registered socket."""
        ...

    def modify_fd(self, fd, events):
        """Change the watched events for an already-registered file
        descriptor."""
        ...

    def remove_socket(self, socket):
        """Unregister ``socket``."""
        ...

    def remove_fd(self, fd):
        """Unregister file descriptor ``fd``."""
        ...

    def remove_timer(self, timer):
        """Unregister ``timer``."""
        ...

    def size(self):
        """Return the number of registered sources."""
        ...

    def wait(self, events, timeout_ms):
        """Wait up to ``timeout_ms`` milliseconds for sources to become ready,
        filling ``events``; a negative timeout blocks indefinitely. Return the
        number of ready sources."""
        ...

    def close(self):
        """Close the poller and release its resources."""
        ...

    def __enter__(self): ...

    def __exit__(self, exc_type, exc, tb): ...

    async def __aenter__(self): ...

    async def __aexit__(self, exc_type, exc, tb): ...
