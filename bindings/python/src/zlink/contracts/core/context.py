# SPDX-License-Identifier: MPL-2.0

_context_factory = None


def register_context_factory(factory):
    global _context_factory
    _context_factory = factory


class Context:
    def __new__(cls):
        if cls is Context:
            if _context_factory is None:
                raise RuntimeError("zlink context runtime is not registered")
            return _context_factory()
        return super().__new__(cls)


class ContextOptions:
    pass
