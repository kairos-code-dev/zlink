# SPDX-License-Identifier: MPL-2.0

from .options import (
    CommonSocketOptions,
    DealerSocketOptions,
    PubSocketOptions,
    RouterSocketOptions,
    StreamSocketOptions,
    SubSocketOptions,
)


# Concrete socket classes live in contracts/sockets/sockets.py. They are
# loaded lazily so that importing contracts/sockets/options.py (which is
# referenced from _runtime/sockets/socket_base.py during its module init)
# does not trigger loading sockets.py (which in turn depends on
# socket_base.py via its private mixin imports).

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
    if name in _SOCKET_NAMES:
        from . import sockets

        value = getattr(sockets, name)
        globals()[name] = value
        return value
    raise AttributeError(f"module {__name__!r} has no attribute {name!r}")

__all__ = [
    "CommonSocketOptions",
    "DealerSocketOptions",
    "StreamSocketOptions",
    "SubSocketOptions",
    "PubSocketOptions",
    "RouterSocketOptions",
    "PairSocket",
    "DealerSocket",
    "RouterSocket",
    "StreamSocket",
    "PubSocket",
    "SubSocket",
    "XPubSocket",
    "XSubSocket",
]
