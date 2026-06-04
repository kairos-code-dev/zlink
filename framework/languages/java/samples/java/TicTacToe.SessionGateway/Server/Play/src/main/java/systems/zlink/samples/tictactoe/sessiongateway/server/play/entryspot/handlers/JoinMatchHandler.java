package systems.zlink.samples.tictactoe.sessiongateway.server.play.entryspot.handlers;

import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.framework.handlers.ZLinkRequest;
import systems.zlink.samples.tictactoe.sessiongateway.server.play.gamespots.TicTacToeGameDirectory;
import systems.zlink.samples.tictactoe.sessiongateway.server.play.gamespots.GameNotificationPublisher;

@ZLinkHandlerGroup("play")
public final class JoinMatchHandler {
    @ZLinkRequest(packetName = "JoinMatchReq")
    public CompletionStage<String> handleAsync(String request) {
        String[] parts = request.split("\\|", -1);
        return CompletableFuture.completedFuture(
            GameNotificationPublisher.encode(
                TicTacToeGameDirectory.get(parts[0]).join(parts[1])));
    }
}
