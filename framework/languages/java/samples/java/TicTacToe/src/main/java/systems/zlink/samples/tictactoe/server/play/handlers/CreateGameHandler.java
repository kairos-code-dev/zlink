package systems.zlink.samples.tictactoe.server.play.handlers;

import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.samples.tictactoe.server.play.gamespots.TicTacToeGameDirectory;

public final class CreateGameHandler {
    public CompletionStage<String> createAsync(String gameName) {
        return CompletableFuture.completedFuture(TicTacToeGameDirectory.create(gameName).gameId());
    }
}
