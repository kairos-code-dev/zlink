package systems.zlink.samples.deliverydispatch.server.courierspotnode.spots.handlers;

import systems.zlink.framework.spots.ZLinkEntrySpotActorSendHandler;
import systems.zlink.framework.spots.ZLinkSpotActorSendContext;
import systems.zlink.samples.deliverydispatch.server.courierspotnode.CourierActor;
import systems.zlink.samples.deliverydispatch.server.courierspotnode.spots.CourierEntrySpot;
import systems.zlink.samples.deliverydispatch.shared.contracts.Messages;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;

public final class CourierDecisionActorHandler
    implements ZLinkEntrySpotActorSendHandler<
        CourierEntrySpot,
        CourierActor,
        Messages.CourierDecision> {
    @Override
    public CompletionStage<Void> handle(
        CourierEntrySpot entrySpot,
        CourierActor actor,
        ZLinkSpotActorSendContext context,
        Messages.CourierDecision message) {
        actor.complete(message);
        return CompletableFuture.completedFuture(null);
    }
}
