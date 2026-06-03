package systems.zlink.samples.tictactoe.sessiongateway.server.play.entryspot.handlers;

import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.samples.tictactoe.sessiongateway.server.play.gamespots.TicTacToeGameDirectory;

public final class JoinMatchHandler implements ZLinkRequestHandler<String, String> {
    @Override
    public CompletionStage<String> handleAsync(String request, ZLinkRequestContext context) {
        String[] parts = request.split("\\|", -1);
        return CompletableFuture.completedFuture(
            TicTacToeGameDirectory.get(parts[0]).join(parts[1]).encode());
    }
}
