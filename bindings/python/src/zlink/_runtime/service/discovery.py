# SPDX-License-Identifier: MPL-2.0
#
# Re-export shim. The discovery service implementation lives in the public
# contract source at zlink/contracts/service/discovery.py.

from ...contracts.service.discovery import *  # noqa: F401,F403


def __getattr__(name):
    from ...contracts.service import discovery as _impl

    try:
        return getattr(_impl, name)
    except AttributeError:
        raise AttributeError(f"module {__name__!r} has no attribute {name!r}") from None
