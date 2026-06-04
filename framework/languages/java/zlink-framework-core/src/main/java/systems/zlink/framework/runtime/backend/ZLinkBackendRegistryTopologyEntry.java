package systems.zlink.framework.runtime.backend;

import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.spots.ZLinkSpotKind;

public record ZLinkBackendRegistryTopologyEntry(
    String autoConnectType,
    RoutingId routingId,
    String serviceKind,
    String serviceRole,
    String channelName,
    String endpoint,
    String source,
    String state,
    int desiredCount,
    int readyCount,
    int errorCode,
    long lastReportedMs,
    ZLinkSpotKind spotKind) {
}
