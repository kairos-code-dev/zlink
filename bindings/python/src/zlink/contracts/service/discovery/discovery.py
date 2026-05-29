# SPDX-License-Identifier: MPL-2.0

from typing import Protocol, runtime_checkable

_registry_factory = None
_discovery_factory = None
_registry_query_client_factory = None


def register_discovery_factories(
    *,
    registry_factory,
    discovery_factory,
    registry_query_client_factory,
):
    global _registry_factory
    global _discovery_factory
    global _registry_query_client_factory
    _registry_factory = registry_factory
    _discovery_factory = discovery_factory
    _registry_query_client_factory = registry_query_client_factory


def _require(factory, name):
    if factory is None:
        raise RuntimeError(f"zlink {name} runtime is not registered")
    return factory


def create_registry(ctx):
    return _require(_registry_factory, "registry")(ctx)


def create_discovery(ctx, auto_connect_type, channel_name: str):
    return _require(_discovery_factory, "discovery")(
        ctx, auto_connect_type, channel_name
    )


def create_registry_query_client(ctx):
    return _require(_registry_query_client_factory, "registry query client")(ctx)


@runtime_checkable
class _ClosableContract(Protocol):
    def close(self): ...

    def __enter__(self): ...

    def __exit__(self, exc_type, exc, tb): ...

    async def __aenter__(self): ...

    async def __aexit__(self, exc_type, exc, tb): ...


@runtime_checkable
class Discovery(_ClosableContract, Protocol):
    def connect_registry(self, registry_endpoint: str): ...

    def set_value(self, value: int): ...

    def get_value(self) -> int: ...

    def member_peers(self): ...

    def resolve_spot(self, spot_rid): ...

    def resolve_actor(self, actor_id): ...

    @property
    def spot_owner_sync_enabled(self): ...

    @spot_owner_sync_enabled.setter
    def spot_owner_sync_enabled(self, enabled): ...

    @property
    def actor_route_sync_enabled(self): ...

    @actor_route_sync_enabled.setter
    def actor_route_sync_enabled(self, enabled): ...

    def set_tls_client(
        self, ca_cert: str | None, hostname: str | None, trust_system: bool = False
    ): ...


def __getattr__(name):
    if name == "SpotRoute":
        from .discovery_models import SpotRoute as value
    elif name == "Registry":
        from ..registry.registry import Registry as value
    elif name == "RegistryQueryClient":
        from ..registry.registry_query_client import RegistryQueryClient as value
    elif name in {
        "MemberPeerEntry",
        "RegistryServiceSummaryEntry",
        "RegistryServiceSummaryFilter",
        "RegistryStatus",
        "RegistryTopologyEntry",
        "RegistryTopologyFilter",
    }:
        from ..registry import registry_models

        value = getattr(registry_models, name)
    else:
        raise AttributeError(f"module {__name__!r} has no attribute {name!r}")
    globals()[name] = value
    return value
