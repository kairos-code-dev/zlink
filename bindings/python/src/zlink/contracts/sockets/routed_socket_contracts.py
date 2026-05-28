# SPDX-License-Identifier: MPL-2.0

from . import socket as _socket_contract


class RouterSocket(_socket_contract._SocketContract):
    def __new__(cls, context=None):
        if cls is RouterSocket:
            return _socket_contract._require(
                _socket_contract._router_socket_factory, "router socket"
            )(context)
        return object.__new__(cls)

    @property
    def router_options(self): ...

    def send(self, routing_id): ...

    def request(self, routing_id): ...

    def reply(self, received): ...

    def recv(self, *, flags=0): ...

    def recv_into(self, received, *, flags=0): ...

    def send_to_spot(self, node_rid, spot_rid): ...

    def request_to_spot(self, node_rid, spot_rid): ...

    def reply_to_spot(self, node_rid, spot_rid, request_seq): ...


__all__ = ["RouterSocket"]
