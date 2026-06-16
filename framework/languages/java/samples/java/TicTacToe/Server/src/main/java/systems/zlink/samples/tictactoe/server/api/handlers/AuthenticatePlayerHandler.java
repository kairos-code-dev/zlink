package systems.zlink.samples.tictactoe.server.api.handlers;

import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.framework.handlers.ZLinkRequest;
import systems.zlink.samples.tictactoe.shared.contracts.AuthenticatePlayerReq;
import systems.zlink.samples.tictactoe.shared.contracts.AuthenticatePlayerRes;

@ZLinkHandlerGroup("api")
public final class AuthenticatePlayerHandler {
    @ZLinkRequest
    public AuthenticatePlayerRes handle(AuthenticatePlayerReq request) {
        String actorId = request.accessToken().trim();
        if (actorId.isBlank()) {
            throw new IllegalArgumentException("authentication token is empty");
        }
        return new AuthenticatePlayerRes(actorId);
    }
}
