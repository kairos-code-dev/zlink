package systems.zlink.samples.supportchat.server.api.handlers;

import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.samples.supportchat.server.configuration.SupportChatRoles;
import systems.zlink.samples.supportchat.shared.contracts.Messages;

@ZLinkHandlerGroup("api")
public final class AuthenticateUserHandler
    implements ZLinkRequestHandler<
        Messages.AuthenticateUserReq,
        Messages.AuthenticateUserRes> {
    @Override
    public Messages.AuthenticateUserRes handle(
        Messages.AuthenticateUserReq request,
        ZLinkRequestContext context) {
        return switch (request.accessToken()) {
            case "customer-1" -> accepted("customer-1", "Customer 1", SupportChatRoles.Customer);
            case "customer-2" -> accepted("customer-2", "Customer 2", SupportChatRoles.Customer);
            case "customer-3" -> accepted("customer-3", "Customer 3", SupportChatRoles.Customer);
            case "agent-1" -> accepted("agent-1", "Agent 1", SupportChatRoles.Agent);
            case "agent-2" -> accepted("agent-2", "Agent 2", SupportChatRoles.Agent);
            case null, default -> new Messages.AuthenticateUserRes(
                false, null, null, null, "Unknown support chat token.");
        };
    }

    private static Messages.AuthenticateUserRes accepted(
        String actorId,
        String displayName,
        String role) {
        return new Messages.AuthenticateUserRes(true, actorId, displayName, role, null);
    }
}
