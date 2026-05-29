# SPDX-License-Identifier: MPL-2.0

_OPTION_NAMES = {
    "CommonSocketOptions",
    "DealerSocketOptions",
    "PubSocketOptions",
    "RouterSocketOptions",
    "StreamSocketOptions",
    "SubSocketOptions",
    "create_common_socket_options",
    "create_dealer_socket_options",
    "create_pub_socket_options",
    "create_router_socket_options",
    "create_stream_socket_options",
    "create_sub_socket_options",
}
_SOCKET_NAMES = {
    "DealerSocket",
    "PairSocket",
    "PubSocket",
    "RouterSocket",
    "StreamSocket",
    "SubSocket",
    "XPubSocket",
    "XSubSocket",
    "create_dealer_socket",
    "create_pair_socket",
    "create_pub_socket",
    "create_router_socket",
    "create_stream_socket",
    "create_sub_socket",
    "create_xpub_socket",
    "create_xsub_socket",
}


def __getattr__(name):
    if name in _OPTION_NAMES:
        from . import socket_options

        value = getattr(socket_options, name)
        globals()[name] = value
        return value
    if name in {
        "create_dealer_socket",
        "create_pair_socket",
        "create_pub_socket",
        "create_router_socket",
        "create_stream_socket",
        "create_sub_socket",
        "create_xpub_socket",
        "create_xsub_socket",
    }:
        from . import socket

        value = getattr(socket, name)
        globals()[name] = value
        return value
    if name in {"DealerSocket", "PairSocket"}:
        from . import message_socket_contracts

        value = getattr(message_socket_contracts, name)
        globals()[name] = value
        return value
    if name == "RouterSocket":
        from .routed_socket_contracts import RouterSocket as value
        globals()[name] = value
        return value
    if name in {"PubSocket", "SubSocket", "XPubSocket", "XSubSocket"}:
        from . import pubsub_socket_contracts

        value = getattr(pubsub_socket_contracts, name)
        globals()[name] = value
        return value
    if name == "StreamSocket":
        from .stream_socket import StreamSocket as value
        globals()[name] = value
        return value
    raise AttributeError(f"module {__name__!r} has no attribute {name!r}")


__all__ = sorted(_OPTION_NAMES | _SOCKET_NAMES)
