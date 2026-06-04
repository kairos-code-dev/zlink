package systems.zlink.samples.bingo.server.play.entryspot;

import systems.zlink.framework.spots.ZLinkEntrySpot;
import systems.zlink.framework.spots.ZLinkEntrySpotContext;
import systems.zlink.samples.bingo.server.play.entryspot.handlers.BingoEntrySpotActorJoinedHandler;
import systems.zlink.samples.bingo.server.play.entryspot.handlers.BingoEntrySpotActorLeftHandler;
import systems.zlink.samples.bingo.server.play.entryspot.handlers.MatchBingoActorHandler;

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
        context.handlers().addHandler(BingoEntrySpotActorJoinedHandler.class);
        context.handlers().addHandler(BingoEntrySpotActorLeftHandler.class);
    }
}
