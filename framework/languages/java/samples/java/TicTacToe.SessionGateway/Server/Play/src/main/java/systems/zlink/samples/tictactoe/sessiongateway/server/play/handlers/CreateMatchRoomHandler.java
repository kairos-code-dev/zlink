package systems.zlink.samples.tictactoe.sessiongateway.server.play.handlers;

import java.util.concurrent.CompletionStage;
import java.util.concurrent.CompletableFuture;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.framework.handlers.ZLinkRequest;
import systems.zlink.samples.tictactoe.sessiongateway.server.play.gamespots.TicTacToeGameDirectory;
import systems.zlink.samples.tictactoe.sessiongateway.server.play.gamespots.TicTacToeGameSpot;

@ZLinkHandlerGroup("play")
public final class CreateMatchRoomHandler {
    private final TicTacToeGameDirectory games;

    public CreateMatchRoomHandler(TicTacToeGameDirectory games) {
        this.games = games;
    }

    @ZLinkRequest(packetName = "CreateMatchRoom")
    public CompletionStage<String> handleAsync(String ownerActorId) {
        TicTacToeGameSpot room = games.create(ownerActorId);
        return CompletableFuture.completedFuture(room.matchId() + "|" + ownerActorId);
    }
}
