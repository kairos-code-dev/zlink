# SPDX-License-Identifier: MPL-2.0

from typing import Protocol, runtime_checkable


@runtime_checkable
class _FluentMessageOp(Protocol):
    """Shared fluent builder for operations that carry message parts.

    Submitting consumes the added parts: on a successful submit each part's
    payload is moved into the transport and the managed message is left empty,
    so a part must not be reused after a successful submit.
    """

    def message(self, payload):
        """Add a message part. It is consumed on a successful submit (see the
        class docstring)."""
        ...

    def messages(self, *payloads):
        """Add several message parts in order; each is consumed on a successful
        submit."""
        ...

    def flags(self, flags):
        """Set the send flags applied at submit time."""
        ...


@runtime_checkable
class SendOp(_FluentMessageOp, Protocol):
    """A multipart send builder: add parts, then submit."""

    def submit(self):
        """Submit the accumulated parts. Return ``True`` when queued, and
        ``False`` only when ``DONT_WAIT`` is set and the send would have blocked
        (back-pressure)."""
        ...


@runtime_checkable
class RequestOp(_FluentMessageOp, Protocol):
    """A request builder: add parts, then submit and await a reply."""

    def timeout(self, timeout):
        """Set how long the request waits for a reply before timing out."""
        ...

    def submit_async(self):
        """Submit the request and asynchronously return the reply parts, which
        the caller owns."""
        ...

    def submit(self, callback):
        """Submit the request; the reply is delivered later to ``callback``,
        which owns the reply parts."""
        ...


@runtime_checkable
class RequestCallbackOp(_FluentMessageOp, Protocol):
    """Callback-submission stage of a request (reached after setting flags)."""

    def timeout(self, timeout):
        """Set how long the request waits for a reply before timing out."""
        ...

    def submit(self, callback):
        """Submit the request; the reply is delivered later to ``callback``."""
        ...


@runtime_checkable
class ReplyOp(_FluentMessageOp, Protocol):
    """A reply builder: add parts, then submit."""

    def submit(self):
        """Submit the accumulated reply parts."""
        ...


@runtime_checkable
class ActorJoinOp(_FluentMessageOp, Protocol):
    """An actor-join builder: add parts, then submit and await the result."""

    def timeout(self, timeout):
        """Set how long the operation waits for completion before timing out."""
        ...

    def submit_async(self):
        """Submit and asynchronously return the join result and reply parts,
        which the caller owns."""
        ...

    def submit(self, callback):
        """Submit; the result and reply parts are delivered later to
        ``callback``."""
        ...


@runtime_checkable
class ActorJoinCallbackOp(_FluentMessageOp, Protocol):
    """Callback-submission stage of an actor join."""

    def timeout(self, timeout):
        """Set how long the operation waits for completion before timing out."""
        ...

    def submit(self, callback):
        """Submit; the result is delivered later to ``callback``."""
        ...


@runtime_checkable
class ActorJoinEntrySpotOp(Protocol):
    """A builder for joining an actor through an entry spot."""

    def timeout(self, timeout):
        """Set how long the operation waits for completion before timing out."""
        ...

    def submit_async(self):
        """Submit and asynchronously return the result."""
        ...

    def submit(self, callback):
        """Submit; the result is delivered later to ``callback``."""
        ...


@runtime_checkable
class ActorJoinReplyOp(_FluentMessageOp, Protocol):
    """A builder for replying to an actor-join request; a zero-part reply is
    allowed."""

    def submit(self):
        """Submit the actor-join reply."""
        ...


@runtime_checkable
class ActorLeaveOp(Protocol):
    """A payload-less actor operation builder (leave, destroy, bind, unbind)."""

    def timeout(self, timeout):
        """Set how long the operation waits for completion before timing out."""
        ...

    def submit_async(self):
        """Submit and asynchronously return the reply parts, which the caller
        owns."""
        ...

    def submit(self, callback):
        """Submit; the reply is delivered later to ``callback``."""
        ...


@runtime_checkable
class ActorDestroyOp(ActorLeaveOp, Protocol):
    """A builder for destroying an actor."""

    ...


@runtime_checkable
class ActorLookupOp(Protocol):
    """A builder for looking up a remote actor's reference."""

    def timeout(self, timeout):
        """Set how long the lookup waits for completion before timing out."""
        ...

    def submit_async(self):
        """Submit and asynchronously return the lookup result."""
        ...

    def submit(self, callback):
        """Submit; the result is delivered later to ``callback``."""
        ...


@runtime_checkable
class ActorBindOp(ActorLeaveOp, Protocol):
    """A builder for binding an actor to a session."""

    ...


@runtime_checkable
class ActorUnbindOp(ActorLeaveOp, Protocol):
    """A builder for removing an actor's session binding."""

    ...


__all__ = [
    "ActorBindOp",
    "ActorDestroyOp",
    "ActorJoinCallbackOp",
    "ActorJoinEntrySpotOp",
    "ActorJoinOp",
    "ActorJoinReplyOp",
    "ActorLeaveOp",
    "ActorLookupOp",
    "ActorUnbindOp",
    "ReplyOp",
    "RequestCallbackOp",
    "RequestOp",
    "SendOp",
]
