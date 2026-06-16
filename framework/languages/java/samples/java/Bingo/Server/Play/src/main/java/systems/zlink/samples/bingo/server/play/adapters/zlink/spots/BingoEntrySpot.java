package systems.zlink.samples.bingo.server.play.adapters.zlink.spots;

import static systems.zlink.framework.ZLinkAwait.await;

import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.spots.ZLinkEntrySpot;
import systems.zlink.framework.spots.ZLinkEntrySpotContext;
import systems.zlink.samples.bingo.server.play.adapters.zlink.actors.PlayerActor;

public final class BingoEntrySpot implements ZLinkEntrySpot {
    private final ZLinkEntrySpotContext context;

    public BingoEntrySpot(ZLinkEntrySpotContext context) {
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
    public void onCreateActor(
        ZLinkActor actor,
        CancellationToken cancellationToken) {
    }

    @Override
    public void onJoinActor(
        ZLinkActor actor,
        CancellationToken cancellationToken) {
        if (actor instanceof PlayerActor player && player.destroyAfterEntrySpotJoin()) {
            await(context.destroyActor(player));
        }
    }

    @Override
    public void onLeaveActor(
        ZLinkActor actor,
        CancellationToken cancellationToken) {
    }

    @Override
    public void onDisconnectActor(
        ZLinkActor actor,
        CancellationToken cancellationToken) {
        if (actor instanceof PlayerActor player) {
            player.markDisconnected();
        }
    }
}
