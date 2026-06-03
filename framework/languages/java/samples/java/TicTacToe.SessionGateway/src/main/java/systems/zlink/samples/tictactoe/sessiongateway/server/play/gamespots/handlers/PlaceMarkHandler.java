package systems.zlink.samples.tictactoe.sessiongateway.server.play.gamespots.handlers;

import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.samples.tictactoe.sessiongateway.server.play.gamespots.TicTacToeGameDirectory;

public final class PlaceMarkHandler implements ZLinkRequestHandler<String, String> {
    @Override
    public CompletionStage<String> handleAsync(String request, ZLinkRequestContext context) {
        String[] parts = request.split("\\|", -1);
        return CompletableFuture.completedFuture(
            TicTacToeGameDirectory.get(parts[0]).placeMark(parts[2], Integer.parseInt(parts[1])).encode());
    }
}
