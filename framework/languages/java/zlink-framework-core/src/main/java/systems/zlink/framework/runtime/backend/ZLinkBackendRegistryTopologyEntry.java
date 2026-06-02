package systems.zlink.framework.runtime.backend;

import systems.zlink.contracts.core.RoutingId;

public record ZLinkBackendRegistryTopologyEntry(
    String channelName,
    RoutingId routingId,
    String serviceKind,
    String endpoint) {
}
