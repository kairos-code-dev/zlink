# SPDX-License-Identifier: MPL-2.0
#
# Re-export shim. The spot service implementation lives in the public
# contract source at zlink/contracts/service/spot.py.

from ...contracts.service.spot import *  # noqa: F401,F403


def __getattr__(name):
    """Forward private (underscore-prefixed) names that internal _runtime
    callers may still import from this module path."""
    from ...contracts.service import spot as _impl

    try:
        return getattr(_impl, name)
    except AttributeError:
        raise AttributeError(f"module {__name__!r} has no attribute {name!r}") from None
