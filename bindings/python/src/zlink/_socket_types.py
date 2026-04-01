# SPDX-License-Identifier: MPL-2.0

import ctypes

from ._enums import RouterOption, SocketType
from ._ffi import lib
from ._core import (
    SubscriptionEvent,
    ZlinkRoutingId,
    _is_eagain,
    _raise_last_error,
    _validated_routing_id_bytes,
)
from ._socket_base import (
    DealerOptionSocket,
    MessageSocket,
    PublisherOptionSocket,
    PublisherSocket,
    RoutingIdSocket,
    RoutedMessageSocket,
    RouterOptionSocket,
    Socket,
    StreamOptionSocket,
    SubscriberOptionSocket,
    SubscriberSocket,
)


class XPubSocketOptions:
    _VERBOSE = 0x3301
    _VERBOSER = 0x3302
    _NO_DROP = 0x3305

    def __init__(self, socket):
        self._socket = socket

    @property
    def verbose(self):
        return self._socket._get_pub_bool_option(self._VERBOSE)

    @verbose.setter
    def verbose(self, enabled):
        self._socket._set_pub_bool_option(self._VERBOSE, enabled)

    @property
    def verboser(self):
        return self._socket._get_pub_bool_option(self._VERBOSER)

    @verboser.setter
    def verboser(self, enabled):
        self._socket._set_pub_bool_option(self._VERBOSER, enabled)

    @property
    def no_drop(self):
        return self._socket._get_pub_bool_option(self._NO_DROP)

    @no_drop.setter
    def no_drop(self, enabled):
        self._socket._set_pub_bool_option(self._NO_DROP, enabled)


class RouterSocketOptions:
    def __init__(self, socket):
        self._socket = socket

    @property
    def mandatory(self):
        return self._socket._get_router_bool_option(RouterOption.MANDATORY)

    @mandatory.setter
    def mandatory(self, enabled):
        self._socket._set_router_bool_option(RouterOption.MANDATORY, enabled)

    @property
    def handover(self):
        return self._socket._get_router_bool_option(RouterOption.HANDOVER)

    @handover.setter
    def handover(self, enabled):
        self._socket._set_router_bool_option(RouterOption.HANDOVER, enabled)

    @property
    def probe(self):
        return self._socket._get_router_bool_option(RouterOption.PROBE)

    @probe.setter
    def probe(self, enabled):
        self._socket._set_router_bool_option(RouterOption.PROBE, enabled)

    @property
    def connect_routing_id(self):
        return self._socket._get_router_bytes_option(RouterOption.CONNECT_ROUTING_ID)

    @connect_routing_id.setter
    def connect_routing_id(self, routing_id):
        self._socket._set_router_bytes_option(
            RouterOption.CONNECT_ROUTING_ID,
            _validated_routing_id_bytes(routing_id),
        )


class PairSocket(MessageSocket):
    _socket_type_value = SocketType.PAIR


class DealerSocket(DealerOptionSocket, RoutingIdSocket, MessageSocket):
    _socket_type_value = SocketType.DEALER


class RouterSocket(RouterOptionSocket, RoutingIdSocket, RoutedMessageSocket):
    _socket_type_value = SocketType.ROUTER

    @property
    def router_options(self):
        return RouterSocketOptions(self)


class StreamSocket(StreamOptionSocket, RoutingIdSocket, RoutedMessageSocket):
    _socket_type_value = SocketType.STREAM


class PubSocket(PublisherOptionSocket, PublisherSocket):
    _socket_type_value = SocketType.PUB


class SubSocket(SubscriberOptionSocket, SubscriberSocket):
    _socket_type_value = SocketType.SUB


class XPubSocket(PublisherOptionSocket, PublisherSocket):
    _socket_type_value = SocketType.XPUB

    @property
    def publisher_options(self):
        return XPubSocketOptions(self)

    def _subscription_event(self, flags):
        routing_id = ZlinkRoutingId()
        subscribed = ctypes.c_int()
        topic_buf = ctypes.create_string_buffer(256)
        topic_len = ctypes.c_size_t(len(topic_buf))
        rc = lib().zlink_subscription_event(
            self._handle,
            ctypes.byref(routing_id),
            ctypes.byref(subscribed),
            topic_buf,
            ctypes.byref(topic_len),
            flags,
        )
        if rc != 0:
            _raise_last_error()
        return SubscriptionEvent(
            topic_buf.raw[: topic_len.value],
            bool(subscribed.value),
            bytes(routing_id.data[: routing_id.size]) or None,
        )

    def recv_subscription_event(self):
        return self._subscription_event(0)

    def try_recv_subscription_event(self):
        try:
            return self._subscription_event(1)
        except Exception as exc:
            if _is_eagain(exc):
                return None
            raise

class XSubSocket(SubscriberOptionSocket, SubscriberSocket):
    _socket_type_value = SocketType.XSUB


for _socket_type, _socket_cls in (
    (SocketType.PAIR, PairSocket),
    (SocketType.DEALER, DealerSocket),
    (SocketType.ROUTER, RouterSocket),
    (SocketType.STREAM, StreamSocket),
    (SocketType.PUB, PubSocket),
    (SocketType.SUB, SubSocket),
    (SocketType.XPUB, XPubSocket),
    (SocketType.XSUB, XSubSocket),
):
    Socket.register_socket_type(_socket_type, _socket_cls)
