package systems.zlink.framework.locations;

import systems.zlink.contracts.core.RoutingId;

public record ZLinkFanoutPublisherDescriptorKey(
    String channelName,
    RoutingId publisherRid) {
}
