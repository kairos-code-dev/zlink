package systems.zlink.samples.bingo.server.api.handlers;

import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.samples.bingo.shared.contracts.Messages;

@ZLinkHandlerGroup("api")
public final class AuthenticatePlayerHandler
    implements ZLinkRequestHandler<
        Messages.AuthenticatePlayerReq,
        Messages.AuthenticatePlayerRes> {
    @Override
    public Messages.AuthenticatePlayerRes handle(
        Messages.AuthenticatePlayerReq request,
        ZLinkRequestContext context) {
        String actorId = request.accessToken();
        return new Messages.AuthenticatePlayerRes(
            true,
            actorId,
            actorId,
            null);
    }
}
