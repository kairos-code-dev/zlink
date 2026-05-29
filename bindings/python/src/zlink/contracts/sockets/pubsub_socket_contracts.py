# SPDX-License-Identifier: MPL-2.0

from . import socket as _socket_contract


class PubSocket(_socket_contract._SocketContract):
    def __new__(cls, context=None):
        if cls is PubSocket:
            return _socket_contract._require(
                _socket_contract._pub_socket_factory, "pub socket"
            )(context)
        return object.__new__(cls)

    @property
    def pub_options(self): ...

    def publish(self, topic): ...


class SubSocket(_socket_contract._SocketContract):
    def __new__(cls, context=None):
        if cls is SubSocket:
            return _socket_contract._require(
                _socket_contract._sub_socket_factory, "sub socket"
            )(context)
        return object.__new__(cls)

    @property
    def sub_options(self): ...

    def subscribe(self, topic): ...

    def unsubscribe(self, topic): ...

    def recv(self, *, flags=0): ...

    def recv_into(self, topic_message, *, flags=0): ...


class XPubSocket(_socket_contract._SocketContract):
    def __new__(cls, context=None):
        if cls is XPubSocket:
            return _socket_contract._require(
                _socket_contract._xpub_socket_factory, "xpub socket"
            )(context)
        return object.__new__(cls)

    @property
    def pub_options(self): ...

    def publish(self, topic): ...

    def receive_subscription_event_into(self, event, *, flags=0): ...


class XSubSocket(_socket_contract._SocketContract):
    def __new__(cls, context=None):
        if cls is XSubSocket:
            return _socket_contract._require(
                _socket_contract._xsub_socket_factory, "xsub socket"
            )(context)
        return object.__new__(cls)

    @property
    def sub_options(self): ...

    def subscribe(self, topic): ...

    def unsubscribe(self, topic): ...


__all__ = ["PubSocket", "SubSocket", "XPubSocket", "XSubSocket"]
