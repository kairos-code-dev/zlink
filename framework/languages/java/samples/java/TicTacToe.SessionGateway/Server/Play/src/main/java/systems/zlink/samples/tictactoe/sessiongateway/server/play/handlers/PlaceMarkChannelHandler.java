package systems.zlink.samples.tictactoe.sessiongateway.server.play.handlers;

import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.framework.handlers.ZLinkRequest;
import systems.zlink.samples.tictactoe.sessiongateway.server.play.gamespots.GameNotificationPublisher;
import systems.zlink.samples.tictactoe.sessiongateway.server.play.gamespots.TicTacToeGameDirectory;

@ZLinkHandlerGroup("play")
public final class PlaceMarkChannelHandler {
    private final TicTacToeGameDirectory games;

    public PlaceMarkChannelHandler(TicTacToeGameDirectory games) {
        this.games = games;
    }

    @ZLinkRequest(packetName = "PlaceMarkReq")
    public CompletionStage<String> handleAsync(String request) {
        String[] parts = request.split("\\|", -1);
        if (parts.length < 3) {
            return CompletableFuture.failedFuture(
                new IllegalArgumentException("PlaceMarkReq payload must contain matchId, cell, and actorId"));
        }
        return CompletableFuture.completedFuture(
            GameNotificationPublisher.encode(
                games.get(parts[0]).placeMark(parts[2], Integer.parseInt(parts[1]))));
    }
}
