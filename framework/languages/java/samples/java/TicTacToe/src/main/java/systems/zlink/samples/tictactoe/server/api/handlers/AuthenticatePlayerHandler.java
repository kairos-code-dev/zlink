package systems.zlink.samples.tictactoe.server.api.handlers;

import java.util.Map;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.framework.handlers.ZLinkRequest;

@ZLinkHandlerGroup("api")
public final class AuthenticatePlayerHandler {
    private static final Map<String, String> Actors = Map.of(
        "alice-token", "alice",
        "bob-token", "bob");

    @ZLinkRequest(packetName = "AuthenticatePlayer")
    public CompletionStage<String> handleAsync(String request) {
        String actorId = Actors.get(request);
        if (actorId == null) {
            throw new IllegalArgumentException("unknown access token");
        }
        return CompletableFuture.completedFuture(actorId);
    }
}
