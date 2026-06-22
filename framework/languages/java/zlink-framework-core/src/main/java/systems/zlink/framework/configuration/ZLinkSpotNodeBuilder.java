package systems.zlink.framework.configuration;

import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.spots.ZLinkEntrySpot;
import systems.zlink.framework.spots.ZLinkSpot;

public interface ZLinkSpotNodeBuilder {
    ZLinkSpotNodeBuilder enableRouter(String endpoint);

    ZLinkSpotNodeBuilder connectRouter(String endpoint);

    ZLinkSpotNodeBuilder connectRouter(RoutingId peerRoutingId, String endpoint);

    ZLinkSpotNodeBuilder setRouterRoutingId(RoutingId routingId);

    ZLinkSpotNodeBuilder enablePubSub(String endpoint);

    ZLinkSpotNodeBuilder connectPeerPub(String endpoint);

    @Deprecated(since = "7.1", forRemoval = false)
    default ZLinkSpotNodeBuilder connectPubSub(String endpoint) {
        return connectPeerPub(endpoint);
    }

    ZLinkSpotNodeBuilder setPubSubRoutingId(RoutingId routingId);

    ZLinkSpotNodeBuilder attachSpotPublisherClient(String channelName);

    ZLinkSpotNodeBuilder attachSpotPublisherClient(String channelName, String endpoint);

    ZLinkSpotNodeBuilder acceptSpotRoutesFromChannel(String channelName);

    ZLinkSpotNodeBuilder acceptSpotRoutesFromChannel(String channelName, String endpoint);

    ZLinkEntrySpotOptions configureEntrySpot();

    ZLinkSpotNodeBuilder addSpotFactory(Class<? extends ZLinkSpot<?>> spotType);

    ZLinkSpotNodeBuilder addEntrySpot(Class<? extends ZLinkEntrySpot<?>> entrySpotType);
}
