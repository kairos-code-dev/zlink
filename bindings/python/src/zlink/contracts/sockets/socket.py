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


def __getattr__(name):
    if name in {"PairSocket", "DealerSocket"}:
        from . import message_socket_contracts

        value = getattr(message_socket_contracts, name)
    elif name == "RouterSocket":
        from .routed_socket_contracts import RouterSocket as value
    elif name in {"PubSocket", "SubSocket", "XPubSocket", "XSubSocket"}:
        from . import pubsub_socket_contracts

        value = getattr(pubsub_socket_contracts, name)
    elif name == "StreamSocket":
        from .stream_socket import StreamSocket as value
    else:
        raise AttributeError(f"module {__name__!r} has no attribute {name!r}")
    globals()[name] = value
    return value
