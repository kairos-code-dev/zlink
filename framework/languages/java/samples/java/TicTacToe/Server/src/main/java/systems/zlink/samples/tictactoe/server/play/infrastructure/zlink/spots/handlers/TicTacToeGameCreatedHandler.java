package systems.zlink.samples.tictactoe.server.play.infrastructure.zlink.spots.handlers;

import systems.zlink.framework.messaging.ZLinkMessage;
import systems.zlink.framework.spots.ZLinkSpotCreateResponse;
import systems.zlink.samples.tictactoe.server.play.infrastructure.zlink.spots.TicTacToeGame;

public final class TicTacToeGameCreatedHandler {
    public ZLinkSpotCreateResponse handle(
        TicTacToeGame game,
        ZLinkMessage request) {
        game.markCreated(request);
        return ZLinkSpotCreateResponse.accept();
    }
}
