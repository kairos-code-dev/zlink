package systems.zlink.samples.tictactoe.server.api.handlers;

import java.util.Map;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;

public final class AuthenticatePlayerHandler implements ZLinkRequestHandler<String, String> {
    private static final Map<String, String> Actors = Map.of(
        "alice-token", "alice",
        "bob-token", "bob");

    @Override
    public CompletionStage<String> handleAsync(String request, ZLinkRequestContext context) {
        String actorId = Actors.get(request);
        if (actorId == null) {
            throw new IllegalArgumentException("unknown access token");
        }
        return CompletableFuture.completedFuture(actorId);
    }
}
