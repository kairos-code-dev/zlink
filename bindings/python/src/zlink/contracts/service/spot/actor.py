# SPDX-License-Identifier: MPL-2.0

from typing import Protocol, runtime_checkable


@runtime_checkable
class Actor(Protocol):
    """A handle to a local actor: join/leave spots, receive its messages, and
    send to its bound session. Supports the context-manager protocol."""

    def ref(self):
        """Return this actor's reference."""
        ...

    @property
    def actor_ref(self):
        """This actor's reference."""
        ...

    def join(self, spot):
        """Begin joining the actor to ``spot``; submit the returned operation to
        apply it."""
        ...

    def leave(self, spot):
        """Begin leaving ``spot``; submit the returned operation to apply it."""
        ...

    def recv_part(self, *, flags=0):
        """Receive the next message part for this actor, or ``None`` when none is
        available."""
        ...

    def send_bound_session(self):
        """Begin a send to the actor's bound session; parts are consumed on a
        successful submit."""
        ...

    def close_bound_session(self, *, timeout=0):
        """Close the actor's bound session, waiting up to ``timeout`` for it to
        drain."""
        ...

    def close(self, *args, **kwargs):
        """Close the actor and release its resources."""
        ...

    def __enter__(self): ...

    def __exit__(self, exc_type, exc, tb): ...

    async def __aenter__(self): ...

    async def __aexit__(self, exc_type, exc, tb): ...


__all__ = ["Actor"]
