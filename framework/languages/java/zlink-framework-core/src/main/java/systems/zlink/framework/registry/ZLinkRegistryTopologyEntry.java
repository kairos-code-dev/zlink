package systems.zlink.framework.registry;

import systems.zlink.contracts.core.RoutingId;

public record ZLinkRegistryTopologyEntry(
    String channelName,
    RoutingId routingId,
    String serviceKind,
    String endpoint) {
}
