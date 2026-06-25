package systems.zlink.framework.configuration;

import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.actors.ZLinkActorFactory;
import systems.zlink.framework.spots.ZLinkEntrySpot;
import systems.zlink.framework.spots.ZLinkSpot;

public interface ZLinkSpotNodeBuilder {
    ZLinkSpotNodeBuilder setRoutingId(RoutingId routingId);

    ZLinkSpotNodeBuilder enableRouter(String endpoint);

    ZLinkSpotNodeBuilder connectRouter(String endpoint);

    ZLinkSpotNodeBuilder connectRouter(RoutingId peerRoutingId, String endpoint);

    ZLinkSpotNodeBuilder enablePubSub(String endpoint);

    ZLinkSpotNodeBuilder connectPeerPub(String endpoint);

    ZLinkEntrySpotOptions configureEntrySpot();

    ZLinkSpotNodeBuilder addSpotFactory(Class<? extends ZLinkSpot<?>> spotType);

    ZLinkSpotNodeBuilder addEntrySpot(Class<? extends ZLinkEntrySpot<?>> entrySpotType);

    ZLinkSpotNodeBuilder addActorFactory(
        String actorType,
        Class<? extends ZLinkActorFactory> factoryType);
}
