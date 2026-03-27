# SPDX-License-Identifier: MPL-2.0

import ctypes

from ._enums import SocketType
from ._ffi import lib
from ._core import ZlinkRoutingId, _raise_last_error
from ._socket_base import MessageSocket, PublisherSocket, Socket, SubscriberSocket


class PairSocket(MessageSocket):
    _socket_type_value = SocketType.PAIR


class DealerSocket(MessageSocket):
    _socket_type_value = SocketType.DEALER

    def set_dealer_option(self, option, value):
        self._set_raw_option(lib().zlink_set_dealer_option, option, value)


class RouterSocket(MessageSocket):
    _socket_type_value = SocketType.ROUTER

    def set_router_option(self, option, value):
        self._set_raw_option(lib().zlink_set_router_option, option, value)

    def get_router_option(self, option, size: int = 256):
        return self._get_raw_option(lib().zlink_get_router_option, option, size)


class StreamSocket(MessageSocket):
    _socket_type_value = SocketType.STREAM

    def set_stream_option(self, option, value):
        self._set_raw_option(lib().zlink_set_stream_option, option, value)

    def get_stream_option(self, option, size: int = 256):
        return self._get_raw_option(lib().zlink_get_stream_option, option, size)


class PubSocket(PublisherSocket):
    _socket_type_value = SocketType.PUB

    def set_pub_option(self, option, value):
        self._set_raw_option(lib().zlink_set_pub_option, option, value)

    def get_pub_option(self, option, size: int = 256):
        return self._get_raw_option(lib().zlink_get_pub_option, option, size)


class SubSocket(SubscriberSocket):
    _socket_type_value = SocketType.SUB

    def set_sub_option(self, option, value):
        self._set_raw_option(lib().zlink_set_sub_option, option, value)

    def get_sub_option(self, option, size: int = 256):
        return self._get_raw_option(lib().zlink_get_sub_option, option, size)


class XPubSocket(PublisherSocket):
    _socket_type_value = SocketType.XPUB

    def set_pub_option(self, option, value):
        self._set_raw_option(lib().zlink_set_pub_option, option, value)

    def get_pub_option(self, option, size: int = 256):
        return self._get_raw_option(lib().zlink_get_pub_option, option, size)

    def subscription_event(self, flags: int = 0):
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
            int(flags),
        )
        if rc != 0:
            _raise_last_error()
        return {
            "routing_id": bytes(routing_id.data[: routing_id.size]) or None,
            "subscribed": bool(subscribed.value),
            "topic": topic_buf.raw[: topic_len.value],
        }


class XSubSocket(SubscriberSocket):
    _socket_type_value = SocketType.XSUB

    def set_sub_option(self, option, value):
        self._set_raw_option(lib().zlink_set_sub_option, option, value)

    def get_sub_option(self, option, size: int = 256):
        return self._get_raw_option(lib().zlink_get_sub_option, option, size)


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
