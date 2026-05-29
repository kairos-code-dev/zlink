# SPDX-License-Identifier: MPL-2.0

from typing import Protocol, runtime_checkable

_utility_implementation = None
_stopwatch_factory = None
_thread_factory = None
_atomic_counter_factory = None


def register_core_implementation(
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
    return _implementation().version()


def strerror(code):
    return _implementation().strerror(code)


def has(capability):
    return _implementation().has(capability)


def proxy(frontend, backend, capture=None):
    return _implementation().proxy(frontend, backend, capture)


def proxy_steerable(frontend, backend, capture, control):
    return _implementation().proxy_steerable(frontend, backend, capture, control)


def sleep(seconds):
    return _implementation().sleep(seconds)


def multipart_close(parts):
    return _implementation().multipart_close(parts)


def create_stopwatch():
    if _stopwatch_factory is None:
        raise RuntimeError("zlink stopwatch runtime is not registered")
    return _stopwatch_factory()


def create_thread(target):
    if _thread_factory is None:
        raise RuntimeError("zlink thread runtime is not registered")
    return _thread_factory(target)


def create_atomic_counter():
    if _atomic_counter_factory is None:
        raise RuntimeError("zlink atomic counter runtime is not registered")
    return _atomic_counter_factory()


@runtime_checkable
class Stopwatch(Protocol):
    def intermediate(self) -> int: ...

    def stop(self) -> int: ...

    def close(self) -> None: ...

    def __enter__(self): ...

    def __exit__(self, exc_type, exc, tb): ...

    async def __aenter__(self): ...

    async def __aexit__(self, exc_type, exc, tb): ...


@runtime_checkable
class Thread(Protocol):
    def join(self) -> None: ...


@runtime_checkable
class AtomicCounter(Protocol):
    def set(self, value: int) -> None: ...

    def increment(self) -> int: ...

    def decrement(self) -> int: ...

    @property
    def value(self) -> int: ...

    def close(self) -> None: ...

    def __enter__(self): ...

    def __exit__(self, exc_type, exc, tb): ...

    async def __aenter__(self): ...

    async def __aexit__(self, exc_type, exc, tb): ...
