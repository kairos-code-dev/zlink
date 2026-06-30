package systems.zlink.e2e.kotlin.yielddispatch;

import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.messaging.ZLinkMessage;
import systems.zlink.framework.spots.ZLinkEntrySpot;
import systems.zlink.framework.spots.ZLinkEntrySpotContext;
import systems.zlink.framework.spots.ZLinkSpotActorJoinResponse;

public final class ProbeEntrySpot implements ZLinkEntrySpot<ProbeActor> {
    private final ZLinkEntrySpotContext context;

    public ProbeEntrySpot(ZLinkEntrySpotContext context) {
        this.context = context;
    }

    @Override
    public ZLinkEntrySpotContext context() {
        return context;
    }

    @Override
    public void configure() {
        context.handlers().addActorRequest(EntryActorJoinHandler.class);
        context.handlers().addActorRequest(EntryActorProbeHandler.class);
        context.handlers().addActorRequest(EntryActorYieldHandler.class);
        context.handlers().addActorRequest(EntryActorFastHandler.class);
    }

    @Override
    public ZLinkSpotActorJoinResponse onActorJoin(
        ProbeActor actor,
        ZLinkMessage request,
        CancellationToken cancellationToken) {
        return ZLinkSpotActorJoinResponse.accept();
    }
}
