# SPDX-License-Identifier: MPL-2.0

_timer_factory = None
_spot_timer_factory = None


def register_timer_factories(*, timer_factory, spot_timer_factory):
    global _timer_factory
    global _spot_timer_factory
    _timer_factory = timer_factory
    _spot_timer_factory = spot_timer_factory


class Timer:
    def __new__(cls):
        if cls is Timer:
            if _timer_factory is None:
                raise RuntimeError("zlink timer runtime is not registered")
            return _timer_factory()
        return super().__new__(cls)

    @classmethod
    def from_spot(cls, spot):
        if cls is Timer:
            if _spot_timer_factory is None:
                raise RuntimeError("zlink spot timer runtime is not registered")
            return _spot_timer_factory(spot)
        raise TypeError("Timer.from_spot is only supported on Timer")

    def start(self, interval_ns: int, repeat_count: int) -> None: ...

    def stop(self) -> None: ...

    def recv(self) -> int | None: ...

    def on_fire(self, handler) -> None: ...

    def close(self) -> None: ...

    def __enter__(self): ...

    def __exit__(self, exc_type, exc, tb): ...

    async def __aenter__(self): ...

    async def __aexit__(self, exc_type, exc, tb): ...
