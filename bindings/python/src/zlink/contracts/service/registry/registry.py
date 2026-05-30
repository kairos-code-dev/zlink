# SPDX-License-Identifier: MPL-2.0

from typing import Protocol, runtime_checkable

from ..discovery import discovery as _discovery_contract


@runtime_checkable
class Registry(_discovery_contract._ClosableContract, Protocol):
    """A service registry that members publish to and peers query for
    topology."""

    REGISTRY_OPT_ID = 0x3801
    REGISTRY_OPT_HEARTBEAT_INTERVAL_MS = 0x3802
    REGISTRY_OPT_HEARTBEAT_TIMEOUT_MS = 0x3803
    REGISTRY_OPT_BROADCAST_INTERVAL_MS = 0x3804

    def bind(self, pub_endpoint: str, router_endpoint: str):
        """Bind the registry's publish and router endpoints so members and peers
        can connect."""
        ...

    def set_option(self, option: int, value: int):
        """Set a registry option by its ``REGISTRY_OPT_*`` code."""
        ...

    def get_option(self, option: int) -> int:
        """Return a registry option by its ``REGISTRY_OPT_*`` code."""
        ...

    def set_id(self, registry_id: int):
        """Set the registry's identifier."""
        ...

    def add_peer(self, peer_pub_endpoint: str):
        """Add a peer registry to federate with, by its publish endpoint."""
        ...

    def set_tls_server(self, cert: str, key: str, require_client_cert: bool = False):
        """Configure the registry as a TLS server; apply before
        :meth:`bind`."""
        ...

    def set_tls_client(
        self, ca_cert: str | None, hostname: str | None, trust_system: bool = False
    ):
        """Configure TLS for connections to peer registries."""
        ...

    def set_heartbeat(self, interval_ms: int, timeout_ms: int):
        """Set the member heartbeat interval and the timeout, both in ms, after
        which a silent member is dropped."""
        ...

    def set_broadcast_interval(self, interval_ms: int):
        """Set how often, in ms, the registry broadcasts its state."""
        ...

    def status(self):
        """Return a snapshot of the registry's current status."""
        ...

    def service_summary(self, filter_=None):
        """Return a per-service rollup of registered endpoints, optionally
        filtered."""
        ...

    def member_peers(self, channel_name):
        """Return the member peers registered on ``channel_name``."""
        ...

    def topology(self, filter_=None):
        """Return the registry's topology entries, optionally filtered."""
        ...


__all__ = ["Registry"]
