# SPDX-License-Identifier: MPL-2.0
#
# Re-export shim. Public utility definitions live in the contract source at
# zlink/contracts/core/utilities.py and zlink/contracts/monitoring/timer.py.
# This module exists for internal _runtime call sites that still address the
# helpers via ``..core.utils``.

from ...contracts.core.utilities import (  # noqa: F401
    AtomicCounter,
    Stopwatch,
    Thread,
    _as_handle,
    _report_unhandled_exception,
    has,
    multipart_close,
    proxy,
    proxy_steerable,
    sleep,
    strerror,
    version,
)
from ...contracts.monitoring.timer import Timer  # noqa: F401


def errno():
    """Internal helper. Raw zlink_errno() is not part of the public surface;
    use ZlinkError.internal_errno on raised exceptions instead."""
    from ..._native.ffi import lib

    return int(lib().zlink_errno())
