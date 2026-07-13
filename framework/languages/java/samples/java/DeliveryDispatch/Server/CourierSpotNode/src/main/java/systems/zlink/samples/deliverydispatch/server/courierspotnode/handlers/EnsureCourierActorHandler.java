package systems.zlink.samples.deliverydispatch.server.courierspotnode.handlers;

import systems.zlink.framework.actors.ZLinkActorManager;
import systems.zlink.framework.spots.ZLinkSpotRequestHandler;
import systems.zlink.samples.deliverydispatch.server.configuration.SampleNames;
import systems.zlink.samples.deliverydispatch.server.courierspotnode.spots.CourierEntrySpot;
import systems.zlink.samples.deliverydispatch.shared.contracts.Messages;
import java.util.concurrent.CompletionStage;

public final class EnsureCourierActorHandler
    implements ZLinkSpotRequestHandler<CourierEntrySpot, Messages.EnsureCourierActorReq, Messages.EnsureCourierActorRes> {
    private final ZLinkActorManager actors;

    public EnsureCourierActorHandler(ZLinkActorManager actors) {
        this.actors = actors;
    }

    @Override
    public CompletionStage<Messages.EnsureCourierActorRes> handle(
        CourierEntrySpot spot,
        Messages.EnsureCourierActorReq request) {
        return actors.getOrCreate(
                request.courierId(), SampleNames.CourierActorType, request)
            .thenApply(actor -> new Messages.EnsureCourierActorRes(
                request.courierId(), systems.zlink.framework.actors.ActorRefSnapshot.from(actor)));
    }
}
