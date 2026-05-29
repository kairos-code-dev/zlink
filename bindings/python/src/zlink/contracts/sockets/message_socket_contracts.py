# SPDX-License-Identifier: MPL-2.0

from typing import Protocol, runtime_checkable

from . import socket as _socket_contract


@runtime_checkable
class PairSocket(_socket_contract._SocketContract, Protocol):
    def connect(self, endpoint): ...

    def disconnect(self, endpoint): ...

    def send(self): ...

    def recv_into(self, received, *, flags=0): ...

    def disconnect_rid(self, peer_rid): ...


@runtime_checkable
class DealerSocket(_socket_contract._SocketContract, Protocol):
    @property
    def dealer_options(self): ...

    def connect(self, endpoint): ...

    def disconnect(self, endpoint): ...

    def send(self): ...

    def request(self): ...

    def recv_into(self, received, *, flags=0): ...


__all__ = ["PairSocket", "DealerSocket"]
