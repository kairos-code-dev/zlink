package systems.zlink.samples.tictactoe.sessiongateway.server.play.handlers;

import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.framework.handlers.ZLinkRequest;
import systems.zlink.samples.tictactoe.sessiongateway.server.play.gamespots.TicTacToeGameDirectory;

@ZLinkHandlerGroup("play")
public final class CreateMatchRoomHandler {
    @ZLinkRequest(packetName = "CreateMatchRoom")
    public CompletionStage<String> handleAsync(String ownerActorId) {
        var room = TicTacToeGameDirectory.create(ownerActorId);
        return CompletableFuture.completedFuture(room.matchId() + "|" + room.ownerActorId());
    }
}
