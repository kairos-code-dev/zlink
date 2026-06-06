package systems.zlink.samples.tictactoe.server.play.gamespots.handlers;

import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.framework.spots.ZLinkSpotCreateResponse;
import systems.zlink.samples.tictactoe.server.play.gamespots.TicTacToeGame;

public final class TicTacToeGameCreatedHandler {
    public CompletionStage<ZLinkSpotCreateResponse> handleAsync(
        TicTacToeGame game,
        Message request) {
        game.markCreated(request);
        return CompletableFuture.completedFuture(ZLinkSpotCreateResponse.accept());
    }
}
