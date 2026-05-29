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
    source_kind: PollSourceKind
    slot: int
    revents: int
    fd: int = 0


def create_poll_events(capacity):
    if _poll_events_factory is None:
        raise RuntimeError("zlink poll events runtime is not registered")
    return _poll_events_factory(capacity)


@runtime_checkable
class PollEvents(Protocol):
    @property
    def capacity(self): ...

    @property
    def ready_count(self): ...

    def source_kind(self, index): ...

    def slot(self, index): ...

    def revents(self, index): ...

    def has_event(self, index, event): ...

    def fd(self, index): ...

    def event(self, index): ...


def create_poller():
    if _poller_factory is None:
        raise RuntimeError("zlink poller runtime is not registered")
    return _poller_factory()


@runtime_checkable
class Poller(Protocol):
    def add_socket(self, socket, events, slot): ...

    def add_fd(self, fd, events, slot): ...

    def add_timer(self, timer, slot): ...

    def modify_socket(self, socket, events): ...

    def modify_fd(self, fd, events): ...

    def remove_socket(self, socket): ...

    def remove_fd(self, fd): ...

    def remove_timer(self, timer): ...

    def size(self): ...

    def wait(self, events, timeout_ms): ...

    def close(self): ...

    def __enter__(self): ...

    def __exit__(self, exc_type, exc, tb): ...

    async def __aenter__(self): ...

    async def __aexit__(self, exc_type, exc, tb): ...
