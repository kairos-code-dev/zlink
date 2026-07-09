package systems.zlink.samples.supportchat.server.api.handlers;

import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.samples.supportchat.server.configuration.SampleNames;
import systems.zlink.samples.supportchat.shared.contracts.Messages;

@ZLinkHandlerGroup(SampleNames.ApiChannel)
public final class AuthenticateUserHandler
    implements ZLinkRequestHandler<Messages.AuthenticateUserReq, Messages.AuthenticateUserRes> {
    @Override
    public Messages.AuthenticateUserRes handle(
        Messages.AuthenticateUserReq request,
        ZLinkRequestContext context) {
        return switch (request.accessToken()) {
            case "customer-token" -> new Messages.AuthenticateUserRes(
                true,
                "customer-1",
                "Customer One",
                "customer",
                null);
            case "agent-token" -> new Messages.AuthenticateUserRes(
                true,
                "agent-1",
                "Agent One",
                "agent",
                null);
            default -> new Messages.AuthenticateUserRes(false, null, null, null, "invalid token");
        };
    }
}
