package systems.zlink.samples.supportchat.server.support.infrastructure.zlink.spots.handlers;

import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.spots.ZLinkSpotActorRequestContext;
import systems.zlink.framework.spots.ZLinkSpotActorRequestHandler;
import systems.zlink.samples.supportchat.server.support.infrastructure.zlink.actors.SupportUserActor;
import systems.zlink.samples.supportchat.server.support.infrastructure.zlink.spots.ConversationSpot;
import systems.zlink.samples.supportchat.shared.contracts.Messages;

public final class SendChatMessageHandler
    implements ZLinkSpotActorRequestHandler<
        ConversationSpot,
        SupportUserActor,
        Messages.SendChatMessageReq,
        Messages.SendChatMessageRes> {
    @Override
    public Messages.SendChatMessageRes handle(
        ConversationSpot spot,
        SupportUserActor actor,
        ZLinkSpotActorRequestContext context,
        Messages.SendChatMessageReq request,
        CancellationToken cancellationToken) {
        return spot.sendMessage(actor, request);
    }
}
