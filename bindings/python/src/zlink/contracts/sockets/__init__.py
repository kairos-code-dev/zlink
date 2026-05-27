# SPDX-License-Identifier: MPL-2.0

_OPTION_NAMES = {
    "CommonSocketOptions",
    "DealerSocketOptions",
    "PubSocketOptions",
    "RouterSocketOptions",
    "StreamSocketOptions",
    "SubSocketOptions",
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
}


def __getattr__(name):
    if name in _OPTION_NAMES:
        from . import options

        value = getattr(options, name)
        globals()[name] = value
        return value
    if name in _SOCKET_NAMES:
        from . import sockets

        value = getattr(sockets, name)
        globals()[name] = value
        return value
    raise AttributeError(f"module {__name__!r} has no attribute {name!r}")


__all__ = sorted(_OPTION_NAMES | _SOCKET_NAMES)
