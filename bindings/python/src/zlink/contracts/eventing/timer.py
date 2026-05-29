# SPDX-License-Identifier: MPL-2.0

from typing import Protocol, runtime_checkable

_timer_factory = None
_spot_timer_factory = None


def register_timer_factories(*, timer_factory, spot_timer_factory):
    global _timer_factory
    global _spot_timer_factory
    _timer_factory = timer_factory
    _spot_timer_factory = spot_timer_factory


def create_timer():
    if _timer_factory is None:
        raise RuntimeError("zlink timer runtime is not registered")
    return _timer_factory()


def create_timer_from_spot(spot):
    if _spot_timer_factory is None:
        raise RuntimeError("zlink spot timer runtime is not registered")
    return _spot_timer_factory(spot)


@runtime_checkable
class Timer(Protocol):
    def start(self, interval_ns: int, repeat_count: int) -> None: ...

    def stop(self) -> None: ...

    def recv(self) -> int | None: ...

    def on_fire(self, handler) -> None: ...

    def close(self) -> None: ...

    def __enter__(self): ...

    def __exit__(self, exc_type, exc, tb): ...

    async def __aenter__(self): ...

    async def __aexit__(self, exc_type, exc, tb): ...
