# SPDX-License-Identifier: MPL-2.0

import ctypes
import asyncio

from ._enums import RouterOption, SocketType
from ._ffi import lib
from ._core import (
    BindError,
    CloseError,
    ConfigError,
    ConnectError,
    HandlerError,
    Message,
    RecvError,
    RecvResult,
    Received,
    RequestError,
    RequestResult,
    RoutingId,
    SubmitError,
    SubmitResult,
    SubscriptionEvent,
    _clone_native_msg,
    _copy_routing_id,
    ZlinkRoutingId,
    _is_eagain,
    _raise_result_error,
    _request_result_from_errno,
    _recv_result_from_errno,
    _submit_result_from_errno,
    _routing_id_bytes,
    _validated_routing_id_bytes,
)
from ._socket_base import (
    _BindSocket,
    _DealerOptionSocket,
    _EndpointSocket,
    _MessageSocket,
    _PublisherOptionSocket,
    _PublisherSocket,
    _RoutingIdSocket,
    _RoutedMessageSocket,
    _RouterOptionSocket,
    _Socket,
    _StreamOptionSocket,
    _SubscriberOptionSocket,
    _SubscriberSocket,
)


class PubSocketOptions:
    _VERBOSE = 0x3301
    _VERBOSER = 0x3302
    _MANUAL = 0x3303
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
    def manual(self):
        try:
            return self._socket._get_pub_bool_option(self._MANUAL)
        except Exception:
            return bool(getattr(self._socket, "_pub_manual_option", False))

    @manual.setter
    def manual(self, enabled):
        self._socket._set_pub_bool_option(self._MANUAL, enabled)
        self._socket._pub_manual_option = bool(enabled)

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
        cached = getattr(self._socket, "_connect_routing_id_option", None)
        if cached is not None:
            return cached
        return RoutingId(
            self._socket._get_router_bytes_option(RouterOption.CONNECT_ROUTING_ID)
        )

    @connect_routing_id.setter
    def connect_routing_id(self, routing_id):
        typed_routing_id = RoutingId(routing_id)
        self._socket._set_router_bytes_option(
            RouterOption.CONNECT_ROUTING_ID,
            typed_routing_id.to_bytes(),
        )
        self._socket._connect_routing_id_option = typed_routing_id


class PairSocket(_EndpointSocket, _MessageSocket):
    _socket_type_value = SocketType.PAIR


class DealerSocket(_EndpointSocket, _DealerOptionSocket, _RoutingIdSocket, _MessageSocket):
    _socket_type_value = SocketType.DEALER


class RouterSocket(
    _EndpointSocket,
    _RouterOptionSocket,
    _RoutingIdSocket,
    _RoutedMessageSocket,
):
    _socket_type_value = SocketType.ROUTER

    @property
    def router_options(self):
        return RouterSocketOptions(self)


class StreamSocket(_BindSocket, _StreamOptionSocket, _RoutingIdSocket, _RoutedMessageSocket):
    _socket_type_value = SocketType.STREAM


class PubSocket(_EndpointSocket, _PublisherOptionSocket, _PublisherSocket):
    _socket_type_value = SocketType.PUB

    @property
    def publisher_options(self):
        return PubSocketOptions(self)


class SubSocket(_EndpointSocket, _SubscriberOptionSocket, _SubscriberSocket):
    _socket_type_value = SocketType.SUB


class XPubSocket(_EndpointSocket, _PublisherOptionSocket, _PublisherSocket):
    _socket_type_value = SocketType.XPUB

    @property
    def publisher_options(self):
        return PubSocketOptions(self)

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
            _raise_result_error(RecvError, RecvResult, rc, lib().zlink_errno())
        return SubscriptionEvent(
            routing_id=_routing_id_bytes(routing_id),
            topic=topic_buf.raw[: topic_len.value],
            subscribed=bool(subscribed.value),
        )

    def receive_subscription_event(self, *, flags=0):
        return self._subscription_event(flags)


class XSubSocket(_EndpointSocket, _SubscriberOptionSocket, _SubscriberSocket):
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
    _Socket.register_socket_type(_socket_type, _socket_cls)
