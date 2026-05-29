# SPDX-License-Identifier: MPL-2.0

from typing import Protocol, runtime_checkable

from ..discovery import discovery as _discovery_contract


@runtime_checkable
class RegistryQueryClient(_discovery_contract._ClosableContract, Protocol):
    def connect(self, endpoint: str): ...

    def topology(self, filter_=None): ...


__all__ = ["RegistryQueryClient"]
