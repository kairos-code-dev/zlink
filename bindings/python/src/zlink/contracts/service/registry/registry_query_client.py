# SPDX-License-Identifier: MPL-2.0

from ..discovery import discovery as _discovery_contract


class RegistryQueryClient(_discovery_contract._ClosableContract):
    def __new__(cls, ctx):
        if cls is RegistryQueryClient:
            return _discovery_contract._require(
                _discovery_contract._registry_query_client_factory,
                "registry query client",
            )(ctx)
        return object.__new__(cls)

    def connect(self, endpoint: str): ...

    def topology(self, filter_=None): ...


__all__ = ["RegistryQueryClient"]
