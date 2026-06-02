package systems.zlink.samples.tictactoe.sessiongateway.server.api.handlers;

import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;

public final class AuthenticateActorHandler implements ZLinkRequestHandler<String, String> {
    @Override
    public CompletionStage<String> handleAsync(String request, ZLinkRequestContext context) {
        if (!request.startsWith("player-")) {
            throw new IllegalArgumentException("unknown actor token");
        }
        return CompletableFuture.completedFuture(request);
    }
}
