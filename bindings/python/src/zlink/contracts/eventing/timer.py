# SPDX-License-Identifier: MPL-2.0

from typing import Protocol, runtime_checkable

_timer_factory = None
_spot_timer_factory = None


def _register_timer_factories(*, timer_factory, spot_timer_factory):
    global _timer_factory
    global _spot_timer_factory
    _timer_factory = timer_factory
    _spot_timer_factory = spot_timer_factory


def create_timer():
    """Create a standalone timer. The caller owns it."""
    if _timer_factory is None:
        raise RuntimeError("zlink timer runtime is not registered")
    return _timer_factory()


def create_timer_from_spot(spot):
    """Create a timer bound to ``spot``'s event loop, so its callbacks run on
    the spot's dispatch thread. The caller owns it."""
    if _spot_timer_factory is None:
        raise RuntimeError("zlink spot timer runtime is not registered")
    return _spot_timer_factory(spot)


@runtime_checkable
class Timer(Protocol):
    """A timer that fires on an interval and can be polled or awaited."""

    def start(self, interval_ns: int, repeat_count: int) -> None:
        """Start the timer firing once per ``interval_ns`` nanoseconds;
        ``repeat_count`` sets how many times it fires."""
        ...

    def stop(self) -> None:
        """Stop the timer; it can be restarted with :meth:`start`."""
        ...

    def recv(self) -> int | None:
        """Receive the next expiration as the cumulative fire count, or ``None``
        when none is pending."""
        ...

    def on_fire(self, handler) -> None:
        """Register ``handler``, invoked on each expiration with the fire count.
        It runs on a background dispatch thread."""
        ...

    def close(self) -> None:
        """Close the timer and release its resources."""
        ...

    def __enter__(self): ...

    def __exit__(self, exc_type, exc, tb): ...

    async def __aenter__(self): ...

    async def __aexit__(self, exc_type, exc, tb): ...
