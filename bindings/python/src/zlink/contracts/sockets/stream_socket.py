# SPDX-License-Identifier: MPL-2.0

from . import socket as _socket_contract


class StreamSocket(_socket_contract._SocketContract):
    def __new__(cls, context=None):
        if cls is StreamSocket:
            return _socket_contract._require(
                _socket_contract._stream_socket_factory, "stream socket"
            )(context)
        return object.__new__(cls)

    @property
    def stream_options(self): ...

    def send(self, routing_id): ...

    def recv(self, *, flags=0): ...

    def recv_into(self, received, *, flags=0): ...

    def attach_actor_gateway(self, node): ...

    def bind_actor(self, session_id, actor_id, *, callback=None): ...

    def unbind_actor(self, session_id, actor_id, *, callback=None): ...

    def send_bound_actor(self, session_id, actor_id): ...

    def bound_actors(self, session_id): ...

    def set_packet_handler(self, handler): ...

    def on_packet(self, handler): ...

    def disconnect_rid(self, peer_rid): ...


__all__ = ["StreamSocket"]
