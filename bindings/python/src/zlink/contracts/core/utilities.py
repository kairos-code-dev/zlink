# SPDX-License-Identifier: MPL-2.0

from typing import Protocol, runtime_checkable

_utility_implementation = None
_stopwatch_factory = None
_thread_factory = None
_atomic_counter_factory = None


def _register_core_implementation(
    runtime,
    *,
    stopwatch_factory,
    thread_factory,
    atomic_counter_factory,
):
    global _utility_implementation
    global _stopwatch_factory
    global _thread_factory
    global _atomic_counter_factory
    _utility_implementation = runtime
    _stopwatch_factory = stopwatch_factory
    _thread_factory = thread_factory
    _atomic_counter_factory = atomic_counter_factory


def _implementation():
    if _utility_implementation is None:
        raise RuntimeError("zlink core runtime is not registered")
    return _utility_implementation


def version():
    """Return the native zlink version as a ``(major, minor, patch)`` tuple."""
    return _implementation().version()


def strerror(code):
    """Return the human-readable message for a native zlink error ``code``."""
    return _implementation().strerror(code)


def has(capability):
    """Return whether the native library was built with the named
    ``capability``."""
    return _implementation().has(capability)


def proxy(frontend, backend, capture=None):
    """Forward messages between ``frontend`` and ``backend`` (optionally
    capturing to ``capture``).

    This blocks the calling thread until the context is terminated, so run it on
    a dedicated thread.
    """
    return _implementation().proxy(frontend, backend, capture)


def proxy_steerable(frontend, backend, capture, control):
    """Forward messages between two sockets under runtime control of
    ``control``. Blocks the calling thread until the proxy is terminated."""
    return _implementation().proxy_steerable(frontend, backend, capture, control)


def sleep(seconds):
    """Block the calling thread for ``seconds``."""
    return _implementation().sleep(seconds)


def multipart_close(parts):
    """Close every message in the multipart payload ``parts``."""
    return _implementation().multipart_close(parts)


def create_stopwatch():
    """Create a high-resolution stopwatch. The caller owns it."""
    if _stopwatch_factory is None:
        raise RuntimeError("zlink stopwatch runtime is not registered")
    return _stopwatch_factory()


def create_thread(target):
    """Create and start a background thread running ``target``. The caller owns
    the returned thread and must join (or close) it."""
    if _thread_factory is None:
        raise RuntimeError("zlink thread runtime is not registered")
    return _thread_factory(target)


def create_atomic_counter():
    """Create a thread-safe integer counter. The caller owns it."""
    if _atomic_counter_factory is None:
        raise RuntimeError("zlink atomic counter runtime is not registered")
    return _atomic_counter_factory()


@runtime_checkable
class Stopwatch(Protocol):
    """A high-resolution elapsed-time stopwatch."""

    def intermediate(self) -> int:
        """Return the elapsed time so far, in microseconds, without stopping."""
        ...

    def stop(self) -> int:
        """Stop the stopwatch and return the total elapsed time in
        microseconds."""
        ...

    def close(self) -> None:
        """Release the stopwatch."""
        ...

    def __enter__(self): ...

    def __exit__(self, exc_type, exc, tb): ...

    async def __aenter__(self): ...

    async def __aexit__(self, exc_type, exc, tb): ...


@runtime_checkable
class Thread(Protocol):
    """A handle to a running background thread (see :func:`create_thread`)."""

    def join(self) -> None:
        """Block the caller until the thread finishes running its target."""
        ...


@runtime_checkable
class AtomicCounter(Protocol):
    """A thread-safe integer counter that can be shared across threads."""

    def set(self, value: int) -> None:
        """Atomically set the value."""
        ...

    def increment(self) -> int:
        """Atomically increment the counter and return the new value."""
        ...

    def decrement(self) -> int:
        """Atomically decrement the counter and return the new value."""
        ...

    @property
    def value(self) -> int:
        """The current value, read atomically."""
        ...

    def close(self) -> None:
        """Release the counter."""
        ...

    def __enter__(self): ...

    def __exit__(self, exc_type, exc, tb): ...

    async def __aenter__(self): ...

    async def __aexit__(self, exc_type, exc, tb): ...
