# SPDX-License-Identifier: MPL-2.0

_DISCOVERY_NAMES = {
    "Discovery",
    "MemberPeerEntry",
    "Registry",
    "RegistryQueryClient",
    "RegistryServiceSummaryEntry",
    "RegistryServiceSummaryFilter",
    "RegistryStatus",
    "RegistryTopologyEntry",
    "RegistryTopologyFilter",
    "SpotRoute",
}
_SPOT_NAMES = {
    "Actor",
    "ActorJoinEntrySpotResult",
    "ActorJoinInfo",
    "ActorJoinRequest",
    "ActorJoinResult",
    "ActorPart",
    "ActorRecvInfo",
    "ActorRef",
    "ActorRoute",
    "Spot",
    "SpotSubscribedPart",
    "SpotDispatchInfo",
    "SpotNode",
    "SpotNodeActorEntry",
    "SpotNodePeerEntry",
    "SpotNodePeerFilter",
    "SpotNodeSocketSnapshotEntry",
    "SpotNodeSocketSnapshotFilter",
    "SpotNodeSpotEntry",
    "SpotNodeStatus",
    "SpotNodeSubjectEntry",
    "SpotNodeSubjectFilter",
    "remote_actor_ref",
}


def __getattr__(name):
    if name in _DISCOVERY_NAMES:
        from . import discovery

        value = getattr(discovery, name)
        globals()[name] = value
        return value
    if name in _SPOT_NAMES:
        from . import spot

        value = getattr(spot, name)
        globals()[name] = value
        return value
    raise AttributeError(f"module {__name__!r} has no attribute {name!r}")


__all__ = sorted(_DISCOVERY_NAMES | _SPOT_NAMES)
