package systems.zlink.framework.configuration;

import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.actors.ZLinkActorFactory;
import systems.zlink.framework.actors.ZLinkRelocationPolicy;
import systems.zlink.framework.spots.ZLinkEntrySpot;
import systems.zlink.framework.spots.ZLinkInstanceSpot;
import systems.zlink.framework.spots.ZLinkSpot;

public interface ZLinkMeshObjectServerBuilder {
    ZLinkMeshObjectServerBuilder addEntrySpot(
        Class<? extends ZLinkEntrySpot<?>> entrySpotType);

    <TSpot extends ZLinkSpot<?>> ZLinkMeshObjectServerBuilder addSpotFactory(
        String stableType,
        Class<TSpot> spotType,
        ZLinkObjectPlacementOptions placement,
        ZLinkRelocationPolicy<TSpot> relocationPolicy);

    <TSpot extends ZLinkInstanceSpot>
    ZLinkMeshObjectServerBuilder addInstanceSpotFactory(
        String stableType,
        Class<TSpot> spotType,
        ZLinkObjectPlacementOptions placement,
        ZLinkRelocationPolicy<TSpot> relocationPolicy);

    <TActor extends ZLinkActor> ZLinkMeshObjectServerBuilder addActorFactory(
        String stableType,
        Class<TActor> actorType,
        Class<? extends ZLinkActorFactory> factoryType,
        ZLinkObjectPlacementOptions placement,
        ZLinkRelocationPolicy<TActor> relocationPolicy);
}
