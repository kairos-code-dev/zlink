package systems.zlink.samples.tictactoe.sessiongateway.server.play.entryspot;

import systems.zlink.framework.spots.ZLinkEntrySpot;
import systems.zlink.framework.spots.ZLinkEntrySpotContext;

public final class TicTacToeEntrySpot implements ZLinkEntrySpot {
    private final ZLinkEntrySpotContext context;

    public TicTacToeEntrySpot() {
        this.context = null;
    }

    public TicTacToeEntrySpot(ZLinkEntrySpotContext context) {
        this.context = context;
    }

    @Override
    public ZLinkEntrySpotContext context() {
        return context;
    }
}
