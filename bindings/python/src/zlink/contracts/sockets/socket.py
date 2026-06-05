# SPDX-License-Identifier: MPL-2.0

from typing import Protocol, runtime_checkable

_pair_socket_factory = None
_dealer_socket_factory = None
_router_socket_factory = None
_stream_socket_factory = None
_pub_socket_factory = None
_sub_socket_factory = None
_xpub_socket_factory = None
_xsub_socket_factory = None


def _register_socket_factories(
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


def create_pair_socket(context=None):
    """Create a PAIR socket on ``context`` (or the default). The caller owns it;
    close it or use it as a context manager."""
    return _require(_pair_socket_factory, "pair socket")(context)


def create_dealer_socket(context=None):
    """Create a DEALER socket on ``context`` (or the default)."""
    return _require(_dealer_socket_factory, "dealer socket")(context)


def create_router_socket(context=None):
    """Create a ROUTER socket on ``context`` (or the default)."""
    return _require(_router_socket_factory, "router socket")(context)


def create_stream_socket(context=None):
    """Create a STREAM socket on ``context`` (or the default)."""
    return _require(_stream_socket_factory, "stream socket")(context)


def create_pub_socket(context=None):
    """Create a PUB socket on ``context`` (or the default)."""
    return _require(_pub_socket_factory, "pub socket")(context)


def create_sub_socket(context=None):
    """Create a SUB socket on ``context`` (or the default)."""
    return _require(_sub_socket_factory, "sub socket")(context)


def create_xpub_socket(context=None):
    """Create an XPUB socket on ``context`` (or the default)."""
    return _require(_xpub_socket_factory, "xpub socket")(context)


def create_xsub_socket(context=None):
    """Create an XSUB socket on ``context`` (or the default)."""
    return _require(_xsub_socket_factory, "xsub socket")(context)


@runtime_checkable
class _SocketContract(Protocol):
    """Operations common to every socket type."""

    def bind(self, endpoint):
        """Bind to a local transport address (for example ``tcp://*:5555``) to
        accept connections there."""
        ...

    def close(self):
        """Close the socket and release its native resources."""
        ...

    def __enter__(self): ...

    def __exit__(self, exc_type, exc, tb): ...

    @property
    def options(self):
        """The typed options facade for this socket."""
        ...


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
