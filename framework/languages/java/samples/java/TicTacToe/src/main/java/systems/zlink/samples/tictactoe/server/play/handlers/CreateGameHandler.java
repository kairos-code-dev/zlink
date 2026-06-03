package systems.zlink.samples.tictactoe.server.play.handlers;

import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.framework.handlers.ZLinkRequest;
import systems.zlink.samples.tictactoe.server.configuration.SampleTopology;
import systems.zlink.samples.tictactoe.server.play.gamespots.TicTacToeGameDirectory;
import systems.zlink.samples.tictactoe.shared.contracts.CreateGameRes;

@ZLinkHandlerGroup("play")
public final class CreateGameHandler {
    @ZLinkRequest(packetName = "CreateGameReq")
    public CompletionStage<CreateGameRes> createAsync(String gameName) {
        String gameId = TicTacToeGameDirectory.create(gameName).gameId();
        return CompletableFuture.completedFuture(new CreateGameRes(
            gameId,
            SampleTopology.PlayStreamEndpoint,
            gameName));
    }
}
