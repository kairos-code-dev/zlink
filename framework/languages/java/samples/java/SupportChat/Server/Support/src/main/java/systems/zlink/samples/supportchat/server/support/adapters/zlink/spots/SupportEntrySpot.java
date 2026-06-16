package systems.zlink.samples.supportchat.server.support.adapters.zlink.spots;

import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.spots.ZLinkEntrySpot;
import systems.zlink.framework.spots.ZLinkEntrySpotContext;
import systems.zlink.samples.supportchat.server.support.adapters.zlink.actors.SupportUserActor;

public final class SupportEntrySpot implements ZLinkEntrySpot {
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
    public void onCreateActor(ZLinkActor actor, CancellationToken cancellationToken) {
    }

    @Override
    public void onJoinActor(ZLinkActor actor, CancellationToken cancellationToken) {
    }

    @Override
    public void onLeaveActor(ZLinkActor actor, CancellationToken cancellationToken) {
    }

    @Override
    public void onDisconnectActor(ZLinkActor actor, CancellationToken cancellationToken) {
        if (actor instanceof SupportUserActor user) {
            user.markDisconnected();
        }
    }
}
