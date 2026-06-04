package systems.zlink.framework.registry;

import systems.zlink.contracts.core.RoutingId;

public record ZLinkMemberPeerEntry(
    String autoConnectType,
    String serviceRole,
    String channelName,
    String endpoint,
    RoutingId routingId,
    long value,
    int weight) {
}
