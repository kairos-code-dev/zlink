package systems.zlink.samples.tictactoe.server.play.entryspot;

import systems.zlink.framework.spots.ZLinkEntrySpot;
import systems.zlink.framework.spots.ZLinkEntrySpotContext;

public final class PlayEntrySpot implements ZLinkEntrySpot {
    private final ZLinkEntrySpotContext context;

    public PlayEntrySpot() {
        this.context = null;
    }

    public PlayEntrySpot(ZLinkEntrySpotContext context) {
        this.context = context;
    }

    @Override
    public ZLinkEntrySpotContext context() {
        return context;
    }
}
