package systems.zlink.framework.runtime.spots;

import java.util.concurrent.CompletionStage;
import java.util.function.Supplier;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.runtime.backend.ZLinkBackendSpot;
import systems.zlink.framework.spots.ZLinkSpot;

abstract class ZLinkSpotContextHost {
    abstract DefaultSpotOutbound createContextOutbound(
        ZLinkBackendSpot backendSpot,
        RoutingId nodeRid);

    abstract ZLinkSpotTimerRegistry createTimerRegistry(
        String spotId,
        ZLinkSpotTimerRegistry.Dispatch dispatch);

    abstract CompletionStage<Void> destroyActorFromEntry(
        RoutingId nodeRid,
        ZLinkActor actor);

    abstract CompletionStage<Void> leaveActor(
        RoutingId nodeRid,
        ZLinkSpot<?> spot,
        ZLinkActor actor,
        String fallbackSpotId);

    abstract CompletionStage<Boolean> closeSpot(String spotId);

    abstract <T> CompletionStage<T> runWithOutbound(
        DefaultSpotOutbound outbound,
        Supplier<CompletionStage<T>> operation);

    abstract CompletionStage<Void> runEntryDispatch(
        Object entryContext,
        Supplier<CompletionStage<Void>> operation);
}
