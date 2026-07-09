package systems.zlink.samples.deliverydispatch.server.courierspotnode.spots;

import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.messaging.ZLinkMessage;
import systems.zlink.framework.spots.ZLinkEntrySpot;
import systems.zlink.framework.spots.ZLinkEntrySpotContext;
import systems.zlink.framework.spots.ZLinkSpotActorJoinResponse;
import systems.zlink.samples.deliverydispatch.server.courierspotnode.ActorDirectory;
import systems.zlink.samples.deliverydispatch.server.courierspotnode.CourierActor;

public final class CourierEntrySpot implements ZLinkEntrySpot<CourierActor> {
    private final ZLinkEntrySpotContext context;
    private final ActorDirectory actors;

    public CourierEntrySpot(
        ZLinkEntrySpotContext context,
        ActorDirectory actors) {
        this.context = context;
        this.actors = actors;
    }

    @Override
    public ZLinkEntrySpotContext context() {
        return context;
    }

    @Override
    public void onCreateActor(
        CourierActor actor,
        ZLinkMessage createRequest,
        CancellationToken cancellationToken) {
        actors.register(actor);
    }

    @Override
    public ZLinkSpotActorJoinResponse onActorJoin(
        CourierActor actor,
        ZLinkMessage request,
        CancellationToken cancellationToken) {
        return ZLinkSpotActorJoinResponse.accept();
    }
}
