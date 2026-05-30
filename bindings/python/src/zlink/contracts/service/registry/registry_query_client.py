# SPDX-License-Identifier: MPL-2.0

from typing import Protocol, runtime_checkable

from ..discovery import discovery as _discovery_contract


@runtime_checkable
class RegistryQueryClient(_discovery_contract._ClosableContract, Protocol):
    """A read-only client that queries a registry for its topology."""

    def connect(self, endpoint: str):
        """Connect to a registry's query endpoint."""
        ...

    def topology(self, filter_=None):
        """Return the registry's topology entries, optionally filtered."""
        ...


__all__ = ["RegistryQueryClient"]
