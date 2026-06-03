package systems.zlink.samples.bingo.server.api.handlers;

import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.samples.bingo.shared.contracts.Messages;

public final class AuthenticatePlayerHandler
    implements ZLinkRequestHandler<Messages.AuthenticatePlayerReq, Messages.AuthenticatePlayerRes> {
    @Override
    public CompletionStage<Messages.AuthenticatePlayerRes> handleAsync(
        Messages.AuthenticatePlayerReq request,
        ZLinkRequestContext context) {
        String actorId = request.accessToken();
        return CompletableFuture.completedFuture(new Messages.AuthenticatePlayerRes(
            true,
            actorId,
            actorId,
            null));
    }
}
