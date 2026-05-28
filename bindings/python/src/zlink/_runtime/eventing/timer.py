# SPDX-License-Identifier: MPL-2.0

import ctypes

from ...contracts.errors.codes import CloseResult, ConfigResult
from ...contracts.errors.errors import CloseError, ConfigError, HandlerError, RecvError
from ...contracts.eventing.timer import Timer
from ...contracts.sockets.codes import HandlerResult, RecvResult
from ..._native.ffi import lib
from ..core.zlink import _as_handle, _report_unhandled_exception
from ..handles.native_support import _raise_result_error


_TIMER_HANDLER = ctypes.CFUNCTYPE(
    None,
    ctypes.c_void_p,
    ctypes.c_uint64,
    ctypes.c_void_p,
)


class NativeTimer(Timer):
    def __init__(self) -> None:
        if hasattr(self, "_handle"):
            return
        self._handle = lib().zlink_timer_new()
        if not self._handle:
            _raise_result_error(ConfigError, ConfigResult, 701, lib().zlink_errno())
        self._handler = None
        self._handler_cb = None

    @classmethod
    def from_spot(cls, spot):
        obj = cls.__new__(cls)
        obj._handle = lib().zlink_spot_timer_new(_as_handle(spot))
        if not obj._handle:
            _raise_result_error(ConfigError, ConfigResult, 701, lib().zlink_errno())
        obj._handler = None
        obj._handler_cb = None
        obj._spot = spot
        if hasattr(spot, "_register_timer"):
            spot._register_timer(obj)
        return obj

    def start(self, interval_ns: int, repeat_count: int) -> None:
        if not self._handle:
            raise ConfigError(ConfigResult.INVALID_HANDLE, lib().zlink_errno())
        rc = lib().zlink_timer_start(self._handle, int(interval_ns), int(repeat_count))
        if rc != 0:
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())

    def stop(self) -> None:
        if not self._handle:
            raise ConfigError(ConfigResult.INVALID_HANDLE, lib().zlink_errno())
        rc = lib().zlink_timer_stop(self._handle)
        if rc != 0:
            _raise_result_error(ConfigError, ConfigResult, rc, lib().zlink_errno())

    def recv(self) -> int | None:
        if not self._handle:
            raise RecvError(RecvResult.INVALID_HANDLE, lib().zlink_errno())
        fire_count = ctypes.c_uint64()
        rc = lib().zlink_timer_recv(self._handle, ctypes.byref(fire_count))
        if rc == RecvResult.NO_DATA:
            return None
        if rc != 0:
            _raise_result_error(RecvError, RecvResult, rc, lib().zlink_errno())
        return int(fire_count.value)

    def on_fire(self, handler) -> None:
        if handler is None:
            raise ValueError("handler must not be None")
        if self._handler_cb is not None:
            raise RuntimeError("handler is already attached")

        def _callback(_timer, fire_count, _userdata):
            try:
                handler(self, int(fire_count))
            except Exception:
                _report_unhandled_exception(handler)

        callback = _TIMER_HANDLER(_callback)
        rc = lib().zlink_timer_handler(self._handle, callback, None)
        if rc != 0:
            _raise_result_error(HandlerError, HandlerResult, rc, lib().zlink_errno())
        self._handler = handler
        self._handler_cb = callback

    def close(self) -> None:
        if not self._handle:
            return
        handle = ctypes.c_void_p(self._handle)
        spot = getattr(self, "_spot", None)
        if spot is not None and hasattr(spot, "_unregister_timer"):
            spot._unregister_timer(self)
        self._handle = None
        self._handler = None
        self._handler_cb = None
        self._spot = None
        rc = lib().zlink_timer_destroy(ctypes.byref(handle))
        if rc != 0:
            _raise_result_error(CloseError, CloseResult, rc, lib().zlink_errno())

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, tb):
        self.close()

    async def __aenter__(self):
        return self

    async def __aexit__(self, exc_type, exc, tb):
        self.close()


NativeTimer.__module__ = "zlink.contracts.eventing.timer"
NativeTimer.__name__ = "Timer"
NativeTimer.__qualname__ = "Timer"


def create_timer():
    return NativeTimer()


def create_timer_from_spot(spot):
    return NativeTimer.from_spot(spot)
