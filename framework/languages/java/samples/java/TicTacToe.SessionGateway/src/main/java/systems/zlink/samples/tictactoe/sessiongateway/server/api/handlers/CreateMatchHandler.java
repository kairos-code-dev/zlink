package systems.zlink.samples.tictactoe.sessiongateway.server.api.handlers;

import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;

public final class CreateMatchHandler implements ZLinkRequestHandler<String, String> {
    @Override
    public CompletionStage<String> handleAsync(String request, ZLinkRequestContext context) {
        return CompletableFuture.completedFuture("match-" + request);
    }
}
