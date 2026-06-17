package systems.zlink.samples.supportchat.server.support.adapters.zlink.spots;

import systems.zlink.contracts.messaging.Message;
import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.spots.ZLinkEntrySpot;
import systems.zlink.framework.spots.ZLinkEntrySpotContext;
import systems.zlink.framework.spots.ZLinkSpotActorJoinResponse;
import systems.zlink.samples.supportchat.server.support.adapters.zlink.actors.SupportUserActor;

public final class SupportEntrySpot implements ZLinkEntrySpot<SupportUserActor> {
    private final ZLinkEntrySpotContext context;

    public SupportEntrySpot(ZLinkEntrySpotContext context) {
        this.context = context;
    }

    @Override
    public ZLinkEntrySpotContext context() {
        return context;
    }

    @Override
    public void configure() {
    }

    @Override
    public void onCreateActor(SupportUserActor actor, CancellationToken cancellationToken) {
    }

    @Override
    public ZLinkSpotActorJoinResponse onActorJoin(
        SupportUserActor actor,
        Message request,
        CancellationToken cancellationToken) {
        return ZLinkSpotActorJoinResponse.accept(Message.from(new byte[0]));
    }

    @Override
    public void onJoinedActor(SupportUserActor actor, CancellationToken cancellationToken) {
    }

    @Override
    public void onLeaveActor(SupportUserActor actor, CancellationToken cancellationToken) {
    }

    @Override
    public void onDisconnectActor(SupportUserActor user, CancellationToken cancellationToken) {
        user.markDisconnected();
    }
}
