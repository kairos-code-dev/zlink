package systems.zlink.samples.tictactoe.sessiongateway.server.api.handlers;

import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.framework.handlers.ZLinkRequest;

@ZLinkHandlerGroup("api")
public final class AuthenticateActorHandler {
    @ZLinkRequest(packetName = "AuthenticateActor")
    public CompletionStage<String> handleAsync(String request) {
        if (!request.startsWith("player-")) {
            throw new IllegalArgumentException("unknown actor token");
        }
        return CompletableFuture.completedFuture(request);
    }
}
