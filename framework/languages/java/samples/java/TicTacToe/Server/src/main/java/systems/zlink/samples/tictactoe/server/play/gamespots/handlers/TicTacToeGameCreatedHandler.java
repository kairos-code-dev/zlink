package systems.zlink.samples.tictactoe.server.play.gamespots.handlers;

import java.util.List;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.samples.tictactoe.server.play.gamespots.TicTacToeGame;

public final class TicTacToeGameCreatedHandler {
    public CompletionStage<Void> handleAsync(
        TicTacToeGame game,
        List<Message> createParts) {
        game.markCreated(createParts);
        return CompletableFuture.completedFuture(null);
    }
}
