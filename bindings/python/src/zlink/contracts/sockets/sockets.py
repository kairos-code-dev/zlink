# SPDX-License-Identifier: MPL-2.0

_pair_socket_factory = None
_dealer_socket_factory = None
_router_socket_factory = None
_stream_socket_factory = None
_pub_socket_factory = None
_sub_socket_factory = None
_xpub_socket_factory = None
_xsub_socket_factory = None
_implementation_socket_types = {}


def register_socket_factories(
    *,
    pair_socket_factory,
    dealer_socket_factory,
    router_socket_factory,
    stream_socket_factory,
    pub_socket_factory,
    sub_socket_factory,
    xpub_socket_factory,
    xsub_socket_factory,
):
    global _pair_socket_factory
    global _dealer_socket_factory
    global _router_socket_factory
    global _stream_socket_factory
    global _pub_socket_factory
    global _sub_socket_factory
    global _xpub_socket_factory
    global _xsub_socket_factory
    _pair_socket_factory = pair_socket_factory
    _dealer_socket_factory = dealer_socket_factory
    _router_socket_factory = router_socket_factory
    _stream_socket_factory = stream_socket_factory
    _pub_socket_factory = pub_socket_factory
    _sub_socket_factory = sub_socket_factory
    _xpub_socket_factory = xpub_socket_factory
    _xsub_socket_factory = xsub_socket_factory


def _require(factory, name):
    if factory is None:
        raise RuntimeError(f"zlink {name} runtime is not registered")
    return factory


def register_socket_implementation_types(**implementation_types):
    _implementation_socket_types.update(implementation_types)


class _SocketContractMeta(type):
    def __instancecheck__(cls, instance):
        implementation_type = _implementation_socket_types.get(cls.__name__)
        if implementation_type is not None and isinstance(instance, implementation_type):
            return True
        return type.__instancecheck__(cls, instance)


class _SocketContract(metaclass=_SocketContractMeta):
    def bind(self, endpoint): ...

    def connect(self, endpoint): ...

    def disconnect(self, endpoint): ...

    def close(self): ...

    def __enter__(self): ...

    def __exit__(self, exc_type, exc, tb): ...

    @property
    def options(self): ...


class PairSocket(_SocketContract):
    def __new__(cls, context=None):
        if cls is PairSocket:
            return _require(_pair_socket_factory, "pair socket")(context)
        return object.__new__(cls)

    def send(self): ...

    def recv(self, *, flags=0): ...

    def recv_into(self, received, *, flags=0): ...

    def disconnect_rid(self, peer_rid): ...


class DealerSocket(_SocketContract):
    def __new__(cls, context=None):
        if cls is DealerSocket:
            return _require(_dealer_socket_factory, "dealer socket")(context)
        return object.__new__(cls)

    @property
    def dealer_options(self): ...

    def send(self): ...

    def request(self): ...

    def recv(self, *, flags=0): ...

    def recv_into(self, received, *, flags=0): ...


class RouterSocket(_SocketContract):
    def __new__(cls, context=None):
        if cls is RouterSocket:
            return _require(_router_socket_factory, "router socket")(context)
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


class StreamSocket(_SocketContract):
    def __new__(cls, context=None):
        if cls is StreamSocket:
            return _require(_stream_socket_factory, "stream socket")(context)
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


class PubSocket(_SocketContract):
    def __new__(cls, context=None):
        if cls is PubSocket:
            return _require(_pub_socket_factory, "pub socket")(context)
        return object.__new__(cls)

    @property
    def pub_options(self): ...

    def publish(self, topic): ...


class SubSocket(_SocketContract):
    def __new__(cls, context=None):
        if cls is SubSocket:
            return _require(_sub_socket_factory, "sub socket")(context)
        return object.__new__(cls)

    @property
    def sub_options(self): ...

    def subscribe(self, topic): ...

    def unsubscribe(self, topic): ...

    def recv(self, *, flags=0): ...

    def recv_into(self, topic_message, *, flags=0): ...


class XPubSocket(_SocketContract):
    def __new__(cls, context=None):
        if cls is XPubSocket:
            return _require(_xpub_socket_factory, "xpub socket")(context)
        return object.__new__(cls)

    @property
    def pub_options(self): ...

    def publish(self, topic): ...

    def recv_subscription(self, *, flags=0): ...


class XSubSocket(_SocketContract):
    def __new__(cls, context=None):
        if cls is XSubSocket:
            return _require(_xsub_socket_factory, "xsub socket")(context)
        return object.__new__(cls)

    @property
    def sub_options(self): ...

    def subscribe(self, topic): ...

    def unsubscribe(self, topic): ...
