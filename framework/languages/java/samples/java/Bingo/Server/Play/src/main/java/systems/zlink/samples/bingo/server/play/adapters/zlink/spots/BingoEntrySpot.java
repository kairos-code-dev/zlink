package systems.zlink.samples.bingo.server.play.adapters.zlink.spots;

import static systems.zlink.framework.ZLinkAwait.await;

import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.actors.ZLinkActor;
import systems.zlink.framework.spots.ZLinkEntrySpot;
import systems.zlink.framework.spots.ZLinkEntrySpotContext;
import systems.zlink.samples.bingo.server.play.adapters.zlink.actors.PlayerActor;
import systems.zlink.samples.bingo.server.play.adapters.zlink.spots.handlers.MatchBingoActorHandler;

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
        context.handlers().addHandler(MatchBingoActorHandler.class);
    }

    @Override
    public void onPostActorJoined(
        ZLinkActor actor,
        CancellationToken cancellationToken) {
        if (actor instanceof PlayerActor player && player.destroyAfterEntrySpotJoin()) {
            await(context.destroyActorAsync(player));
        }
    }

    @Override
    public void onActorLeft(
        ZLinkActor actor,
        CancellationToken cancellationToken) {
    }
}
