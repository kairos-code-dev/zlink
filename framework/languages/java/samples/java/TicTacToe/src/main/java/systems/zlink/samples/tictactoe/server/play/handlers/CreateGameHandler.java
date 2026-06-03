package systems.zlink.samples.tictactoe.server.play.handlers;

import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.framework.handlers.ZLinkRequest;
import systems.zlink.samples.tictactoe.server.play.gamespots.TicTacToeGameDirectory;

@ZLinkHandlerGroup("play")
public final class CreateGameHandler {
    @ZLinkRequest(packetName = "CreateGameReq")
    public CompletionStage<String> createAsync(String gameName) {
        return CompletableFuture.completedFuture(TicTacToeGameDirectory.create(gameName).gameId());
    }
}
