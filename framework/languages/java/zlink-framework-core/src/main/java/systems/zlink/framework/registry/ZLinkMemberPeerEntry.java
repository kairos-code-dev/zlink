package systems.zlink.framework.registry;

import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.service.registry.AutoConnectType;
import systems.zlink.contracts.service.registry.ServiceRole;

public record ZLinkMemberPeerEntry(
    AutoConnectType autoConnectType,
    ServiceRole serviceRole,
    String channelName,
    String endpoint,
    RoutingId routingId,
    long value,
    int weight) {
}
