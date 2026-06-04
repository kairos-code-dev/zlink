package systems.zlink.samples.bingo.server.api.handlers;

import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.framework.handlers.ZLinkRequest;
import systems.zlink.samples.bingo.shared.contracts.Messages;

@ZLinkHandlerGroup("api")
public final class AuthenticatePlayerHandler {
    @ZLinkRequest(packetName = "AuthenticatePlayer")
    public CompletionStage<Messages.AuthenticatePlayerRes> handleAsync(
        Messages.AuthenticatePlayerReq request) {
        String actorId = request.accessToken();
        return CompletableFuture.completedFuture(new Messages.AuthenticatePlayerRes(
            true,
            actorId,
            actorId,
            null));
    }
}
