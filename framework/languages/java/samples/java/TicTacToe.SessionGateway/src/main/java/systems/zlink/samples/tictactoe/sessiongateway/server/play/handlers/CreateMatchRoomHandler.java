package systems.zlink.samples.tictactoe.sessiongateway.server.play.handlers;

import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.samples.tictactoe.sessiongateway.server.play.gamespots.TicTacToeGameDirectory;

public final class CreateMatchRoomHandler implements ZLinkRequestHandler<String, String> {
    @Override
    public CompletionStage<String> handleAsync(String ownerActorId, ZLinkRequestContext context) {
        var room = TicTacToeGameDirectory.create(ownerActorId);
        return CompletableFuture.completedFuture(room.matchId() + "|" + room.ownerActorId());
    }
}
