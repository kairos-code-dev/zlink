# SPDX-License-Identifier: MPL-2.0

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


class Stopwatch:
    def __new__(cls):
        if cls is Stopwatch:
            if _stopwatch_factory is None:
                raise RuntimeError("zlink stopwatch runtime is not registered")
            return _stopwatch_factory()
        return super().__new__(cls)


class Thread:
    def __new__(cls, target):
        if cls is Thread:
            if _thread_factory is None:
                raise RuntimeError("zlink thread runtime is not registered")
            return _thread_factory(target)
        return super().__new__(cls)


class AtomicCounter:
    def __new__(cls):
        if cls is AtomicCounter:
            if _atomic_counter_factory is None:
                raise RuntimeError("zlink atomic counter runtime is not registered")
            return _atomic_counter_factory()
        return super().__new__(cls)
