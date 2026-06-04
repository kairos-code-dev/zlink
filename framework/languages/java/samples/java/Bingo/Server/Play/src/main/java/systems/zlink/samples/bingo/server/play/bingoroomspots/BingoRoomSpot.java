package systems.zlink.samples.bingo.server.play.bingoroomspots;

import java.util.List;
import systems.zlink.framework.spots.ZLinkSpot;
import systems.zlink.framework.spots.ZLinkSpotContext;

public final class BingoRoomSpot implements ZLinkSpot {
    private final ZLinkSpotContext context;

    public BingoRoomSpot(ZLinkSpotContext context) {
        this.context = context;
    }

    @Override
    public ZLinkSpotContext context() {
        return context;
    }

    static BingoCard cardFor(String playerId) {
        return switch (playerId) {
            case "player-2", "player-3" -> new BingoCard(List.of(7, 11, 42));
            default -> new BingoCard(List.of(1, 2, 3));
        };
    }
}
