package systems.zlink.samples.tictactoe.server.play.adapters.zlink.spots;

import static systems.zlink.framework.ZLinkAwait.await;

import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.spots.ZLinkEntrySpot;
import systems.zlink.framework.spots.ZLinkEntrySpotContext;
import systems.zlink.samples.tictactoe.server.play.adapters.zlink.actors.PlayActor;

public final class PlayEntrySpot implements ZLinkEntrySpot {
    private final ZLinkEntrySpotContext context;

    public PlayEntrySpot(ZLinkEntrySpotContext context) {
        this.context = context;
    }

    @Override
    public ZLinkEntrySpotContext context() {
        return context;
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
        if (actor instanceof PlayActor player && player.destroyAfterEntrySpotJoin()) {
            await(context.destroyActor(player));
        }
    }

    @Override
    public void onDisconnectActor(
        ZLinkActor actor,
        CancellationToken cancellationToken) {
        if (actor instanceof PlayActor player) {
            player.markDisconnected();
        }
    }
}
