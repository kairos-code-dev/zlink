# SPDX-License-Identifier: MPL-2.0

from typing import Protocol, runtime_checkable

_context_factory = None


def register_context_factory(factory):
    global _context_factory
    _context_factory = factory


def create_context():
    if _context_factory is None:
        raise RuntimeError("zlink context runtime is not registered")
    return _context_factory()


@runtime_checkable
class Context(Protocol):
    @property
    def options(self): ...

    def recalculate_auto_hwm(self): ...

    def shutdown(self): ...

    def close(self): ...

    def __enter__(self): ...

    def __exit__(self, exc_type, exc, tb): ...

    async def __aenter__(self): ...

    async def __aexit__(self, exc_type, exc, tb): ...


@runtime_checkable
class ContextOptions(Protocol):
    pass
