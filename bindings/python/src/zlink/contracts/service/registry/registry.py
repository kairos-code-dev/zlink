# SPDX-License-Identifier: MPL-2.0

from ..discovery import discovery as _discovery_contract


class Registry(_discovery_contract._ClosableContract):
    REGISTRY_OPT_ID = 0x3801
    REGISTRY_OPT_HEARTBEAT_INTERVAL_MS = 0x3802
    REGISTRY_OPT_HEARTBEAT_TIMEOUT_MS = 0x3803
    REGISTRY_OPT_BROADCAST_INTERVAL_MS = 0x3804

    def __new__(cls, ctx):
        if cls is Registry:
            return _discovery_contract._require(
                _discovery_contract._registry_factory, "registry"
            )(ctx)
        return object.__new__(cls)

    def bind(self, pub_endpoint: str, router_endpoint: str): ...

    def set_option(self, option: int, value: int): ...

    def get_option(self, option: int) -> int: ...

    def set_id(self, registry_id: int): ...

    def add_peer(self, peer_pub_endpoint: str): ...

    def set_tls_server(self, cert: str, key: str, require_client_cert: bool = False): ...

    def set_tls_client(
        self, ca_cert: str | None, hostname: str | None, trust_system: bool = False
    ): ...

    def set_heartbeat(self, interval_ms: int, timeout_ms: int): ...

    def set_broadcast_interval(self, interval_ms: int): ...

    def status(self): ...

    def service_summary(self, filter_=None): ...

    def member_peers(self, channel_name): ...

    def topology(self, filter_=None): ...


__all__ = ["Registry"]
