# SPDX-License-Identifier: MPL-2.0

from typing import Protocol, runtime_checkable

from . import socket as _socket_contract


@runtime_checkable
class StreamSocket(_socket_contract._SocketContract, Protocol):
    """STREAM socket: exchanges framed packets with raw TCP peers."""

    @property
    def stream_options(self):
        """The STREAM-specific typed options facade."""
        ...

    def send(self, routing_id):
        """Begin a send addressed to ``routing_id``; parts are consumed on a
        successful submit."""
        ...

    def recv_into(self, received, *, flags=0):
        """Receive a message into ``received`` storage; ``False`` when
        ``DONT_WAIT`` is set and none is available."""
        ...

    def bind_actor(self, session_id, actor_id, *, callback=None):
        """Bind actor ``actor_id`` to session ``session_id``; the result is
        delivered to ``callback`` when given."""
        ...

    def unbind_actor(self, session_id, actor_id, *, callback=None):
        """Remove the binding of ``actor_id`` from session ``session_id``; the
        result is delivered to ``callback`` when given."""
        ...

    def send_bound_actor(self, session_id, actor_id):
        """Begin a send addressed to the bound actor ``actor_id`` on session
        ``session_id``; parts are consumed on a successful submit."""
        ...

    def bound_actors(self, session_id):
        """Return the actors currently bound to session ``session_id``."""
        ...

    def on_packet(self, handler):
        """Register ``handler``, invoked for each inbound framed packet with the
        sender routing id, header, and body; it owns the two messages and runs
        on a background dispatch thread."""
        ...

    def disconnect_rid(self, peer_rid):
        """Disconnect the peer identified by ``peer_rid``."""
        ...


__all__ = ["StreamSocket"]
