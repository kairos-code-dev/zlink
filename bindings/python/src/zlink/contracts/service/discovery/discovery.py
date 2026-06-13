# SPDX-License-Identifier: MPL-2.0

from typing import Protocol, runtime_checkable


@runtime_checkable
class _ClosableContract(Protocol):
    """Base for closable resources that support the context-manager protocol."""

    def close(self):
        """Close the resource and release its native resources."""
        ...

    def __enter__(self): ...

    def __exit__(self, exc_type, exc, tb): ...

    async def __aenter__(self): ...

    async def __aexit__(self, exc_type, exc, tb): ...


@runtime_checkable
class Discovery(_ClosableContract, Protocol):
    """A discovery service that learns peer routes from a registry and resolves
    spots and actors for a fixed channel."""

    def connect_registry(self, registry_endpoint: str):
        """Connect to a registry's publish endpoint to receive route
        updates."""
        ...

    def set_value(self, value: int):
        """Set the application-defined value advertised for this peer."""
        ...

    def get_value(self) -> int:
        """Return the application-defined value advertised for this peer."""
        ...

    def member_peers(self):
        """Return the registry member peers currently known to this service."""
        ...

    def resolve_spot(self, spot_rid):
        """Resolve the route to the spot identified by ``spot_rid``."""
        ...

    def resolve_actor(self, actor_id):
        """Resolve the route to the actor identified by ``actor_id``."""
        ...

    def bind_route(self, kind: int, key, value):
        """Bind a custom route value under ``kind`` and ``key``."""
        ...

    def unbind_route(self, kind: int, key):
        """Remove the custom route bound under ``kind`` and ``key``."""
        ...

    def resolve_route(self, kind: int, key):
        """Resolve the custom route bound under ``kind`` and ``key``."""
        ...

    @property
    def spot_owner_sync_enabled(self):
        """Whether spot ownership changes are synchronized across peers."""
        ...

    @spot_owner_sync_enabled.setter
    def spot_owner_sync_enabled(self, enabled): ...

    @property
    def actor_route_sync_enabled(self):
        """Whether actor routes are synchronized across peers."""
        ...

    @actor_route_sync_enabled.setter
    def actor_route_sync_enabled(self, enabled): ...

    def set_tls_client(
        self, ca_cert: str | None, hostname: str | None, trust_system: bool = False
    ):
        """Configure TLS for the registry connection; apply before
        :meth:`connect_registry`. ``ca_cert`` is the CA bundle path, ``hostname``
        the expected registry hostname, and ``trust_system`` also trusts the
        system CA store."""
        ...


def __getattr__(name):
    if name == "SpotRoute":
        from .discovery_models import SpotRoute as value
    elif name == "DiscoveryRoute":
        from .discovery_models import DiscoveryRoute as value
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
