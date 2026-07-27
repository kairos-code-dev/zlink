package systems.zlink.framework.runtime.internal.configuration;

import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.actors.ZLinkActorFactory;
import systems.zlink.framework.actors.ZLinkActorTransferAdapter;
import systems.zlink.framework.spots.ZLinkEntrySpot;
import systems.zlink.framework.spots.ZLinkSpot;

public interface ZLinkSpotNodeBuilder {
    ZLinkSpotNodeBuilder setRoutingId(RoutingId routingId);

    ZLinkSpotNodeBuilder enableRouter(String endpoint);

    ZLinkSpotNodeBuilder connectRouter(String endpoint);

    ZLinkSpotNodeBuilder connectRouter(RoutingId peerRoutingId, String endpoint);

    ZLinkSpotNodeBuilder enablePubSub(String endpoint);

    ZLinkSpotNodeBuilder connectPeerPub(String endpoint);

    ZLinkSpotNodeBuilder addSpotFactory(Class<? extends ZLinkSpot<?>> spotType);

    ZLinkSpotNodeBuilder addEntrySpot(Class<? extends ZLinkEntrySpot<?>> entrySpotType);

    /**
     * Registers the factory that defines an actor type string. Actor creation
     * APIs pass this string, while actor lookup APIs use actor id only.
     */
    ZLinkSpotNodeBuilder addActorFactory(
        String actorType,
        Class<? extends ZLinkActorFactory> factoryType);

    /**
     * Registers the domain-state adapter used when this actor type moves to a
     * different Spot node. Actor types without an adapter use the framework's
     * empty-state transfer path.
     */
    ZLinkSpotNodeBuilder addActorTransferAdapter(
        String actorType,
        Class<? extends ZLinkActorTransferAdapter<?>> adapterType);
}
