package systems.zlink.samples.deliverydispatch.server.courierspotnode.spots.handlers;

import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.spots.ZLinkEntrySpotActorRequestHandler;
import systems.zlink.framework.spots.ZLinkSpotActorRequestContext;
import systems.zlink.samples.deliverydispatch.server.courierspotnode.CourierActor;
import systems.zlink.samples.deliverydispatch.server.courierspotnode.spots.CourierEntrySpot;
import systems.zlink.samples.deliverydispatch.shared.contracts.Messages;

public final class BindCourierSessionActorHandler
    implements ZLinkEntrySpotActorRequestHandler<
        CourierEntrySpot,
        CourierActor,
        Messages.BindCourierSession,
        Messages.BindCourierSessionAccepted> {
    @Override
    public Messages.BindCourierSessionAccepted handle(
        CourierEntrySpot entrySpot,
        CourierActor actor,
        ZLinkSpotActorRequestContext context,
        Messages.BindCourierSession request,
        CancellationToken cancellationToken) {
        return new Messages.BindCourierSessionAccepted(
            request.courierId(),
            request.actor().nodeRid(),
            request.sessionRoute());
    }
}
