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
        context.handlers().addActorRequest(YieldProbeHandlers.ActorJoinHandler.class);
        context.handlers().addActorRequest(YieldProbeHandlers.ActorYieldHandler.class);
        context.handlers().addActorRequest(YieldProbeHandlers.ActorFastHandler.class);
        context.handlers().addActorRequest(YieldProbeHandlers.ActorJoinYieldHandler.class);
        context.handlers().addActorRequest(YieldProbeHandlers.ActorPushYieldHandler.class);
    }

    @Override
    public ZLinkSpotActorJoinResponse onActorJoin(
        YieldActor actor,
        ZLinkMessage request,
        CancellationToken cancellationToken) {
        return ZLinkSpotActorJoinResponse.accept();
    }
}
