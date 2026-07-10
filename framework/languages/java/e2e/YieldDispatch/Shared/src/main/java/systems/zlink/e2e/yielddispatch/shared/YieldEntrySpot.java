package systems.zlink.e2e.yielddispatch.shared;

import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.messaging.ZLinkMessage;
import systems.zlink.framework.spots.ZLinkEntrySpot;
import systems.zlink.framework.spots.ZLinkEntrySpotContext;
import systems.zlink.framework.spots.ZLinkSpotActorJoinResponse;

public final class YieldEntrySpot implements ZLinkEntrySpot<YieldActor> {
    private final ZLinkEntrySpotContext context;

    public YieldEntrySpot(ZLinkEntrySpotContext context) {
        this.context = context;
    }

    @Override
    public ZLinkEntrySpotContext context() {
        return context;
    }

    @Override
    public void configure() {
        context.handlers().addHandler(YieldProbeHandlers.ActorJoinHandler.class);
        context.handlers().addHandler(YieldProbeHandlers.ActorYieldHandler.class);
        context.handlers().addHandler(YieldProbeHandlers.ActorFastHandler.class);
        context.handlers().addHandler(YieldProbeHandlers.ActorJoinYieldHandler.class);
        context.handlers().addHandler(YieldProbeHandlers.ActorPushNotifyYieldHandler.class);
    }

    @Override
    public ZLinkSpotActorJoinResponse onActorJoin(
        String actorId,
        ZLinkMessage request,
        CancellationToken cancellationToken) {
        return ZLinkSpotActorJoinResponse.accept();
    }

    @Override
    public void onJoinedActor(YieldActor actor, CancellationToken cancellationToken) {
    }

    @Override
    public void onLeaveActor(YieldActor actor, CancellationToken cancellationToken) {
    }
}
