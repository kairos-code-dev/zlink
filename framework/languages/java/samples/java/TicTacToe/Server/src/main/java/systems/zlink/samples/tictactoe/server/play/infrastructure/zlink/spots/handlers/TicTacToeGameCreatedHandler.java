package systems.zlink.samples.tictactoe.server.play.infrastructure.zlink.spots.handlers;

import systems.zlink.contracts.messaging.Message;
import systems.zlink.framework.spots.ZLinkSpotCreateResponse;
import systems.zlink.samples.tictactoe.server.play.infrastructure.zlink.spots.TicTacToeGame;

public final class TicTacToeGameCreatedHandler {
    public ZLinkSpotCreateResponse handle(
        TicTacToeGame game,
        Message request) {
        game.markCreated(request);
        return ZLinkSpotCreateResponse.accept();
    }
}
