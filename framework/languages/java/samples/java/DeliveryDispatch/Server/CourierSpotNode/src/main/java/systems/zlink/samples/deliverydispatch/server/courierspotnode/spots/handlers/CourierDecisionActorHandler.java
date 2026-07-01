package systems.zlink.samples.deliverydispatch.server.courierspotnode.spots.handlers;

import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.spots.ZLinkEntrySpotActorSendHandler;
import systems.zlink.framework.spots.ZLinkSpotActorSendContext;
import systems.zlink.samples.deliverydispatch.server.courierspotnode.CourierActor;
import systems.zlink.samples.deliverydispatch.server.courierspotnode.spots.CourierEntrySpot;
import systems.zlink.samples.deliverydispatch.shared.contracts.Messages;

public final class CourierDecisionActorHandler
    implements ZLinkEntrySpotActorSendHandler<
        CourierEntrySpot,
        CourierActor,
        Messages.CourierDecision> {
    @Override
    public void handle(
        CourierEntrySpot entrySpot,
        CourierActor actor,
        ZLinkSpotActorSendContext context,
        Messages.CourierDecision message,
        CancellationToken cancellationToken) {
        actor.complete(message);
    }
}
