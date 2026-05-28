# SPDX-License-Identifier: MPL-2.0

from . import socket as _socket_contract


class PairSocket(_socket_contract._SocketContract):
    def __new__(cls, context=None):
        if cls is PairSocket:
            return _socket_contract._require(
                _socket_contract._pair_socket_factory, "pair socket"
            )(context)
        return object.__new__(cls)

    def send(self): ...

    def recv(self, *, flags=0): ...

    def recv_into(self, received, *, flags=0): ...

    def disconnect_rid(self, peer_rid): ...


class DealerSocket(_socket_contract._SocketContract):
    def __new__(cls, context=None):
        if cls is DealerSocket:
            return _socket_contract._require(
                _socket_contract._dealer_socket_factory, "dealer socket"
            )(context)
        return object.__new__(cls)

    @property
    def dealer_options(self): ...

    def send(self): ...

    def request(self): ...

    def recv(self, *, flags=0): ...

    def recv_into(self, received, *, flags=0): ...


__all__ = ["PairSocket", "DealerSocket"]
