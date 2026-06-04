package systems.zlink.framework.runtime.backend;

import systems.zlink.contracts.core.RoutingId;

public record ZLinkBackendRegistryMemberPeerEntry(
    String autoConnectType,
    String serviceRole,
    String channelName,
    String endpoint,
    RoutingId routingId,
    long value,
    int weight) {
}
