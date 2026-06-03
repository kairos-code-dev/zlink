package systems.zlink.samples.tictactoe.server.api.handlers;

import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.framework.handlers.ZLinkRequest;
import systems.zlink.samples.tictactoe.shared.contracts.AuthenticatePlayerReq;
import systems.zlink.samples.tictactoe.shared.contracts.AuthenticatePlayerRes;

@ZLinkHandlerGroup("api")
public final class AuthenticatePlayerHandler {
    @ZLinkRequest(packetName = "AuthenticatePlayer")
    public CompletionStage<AuthenticatePlayerRes> handleAsync(AuthenticatePlayerReq request) {
        String actorId = request.accessToken().trim();
        if (actorId.isBlank()) {
            throw new IllegalArgumentException("authentication token is empty");
        }
        return CompletableFuture.completedFuture(new AuthenticatePlayerRes(actorId));
    }
}
