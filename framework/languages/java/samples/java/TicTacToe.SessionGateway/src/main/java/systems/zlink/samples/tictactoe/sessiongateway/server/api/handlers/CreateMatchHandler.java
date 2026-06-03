package systems.zlink.samples.tictactoe.sessiongateway.server.api.handlers;

import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.framework.handlers.ZLinkRequest;

@ZLinkHandlerGroup("api")
public final class CreateMatchHandler {
    @ZLinkRequest(packetName = "CreateMatch")
    public CompletionStage<String> handleAsync(String request) {
        return CompletableFuture.completedFuture("match-" + request);
    }
}
