package systems.zlink.framework.registry;

import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.service.registry.AutoConnectType;
import systems.zlink.contracts.service.registry.ServiceKind;
import systems.zlink.contracts.service.registry.ServiceRole;
import systems.zlink.contracts.service.registry.TopologySource;
import systems.zlink.contracts.service.registry.TopologyState;
import systems.zlink.framework.spots.ZLinkSpotKind;

public record ZLinkRegistryTopologyEntry(
    AutoConnectType autoConnectType,
    RoutingId routingId,
    ServiceKind serviceKind,
    ServiceRole serviceRole,
    String channelName,
    String endpoint,
    TopologySource source,
    TopologyState state,
    int desiredCount,
    int readyCount,
    int errorCode,
    long lastReportedMs,
    ZLinkSpotKind spotKind) {
}
