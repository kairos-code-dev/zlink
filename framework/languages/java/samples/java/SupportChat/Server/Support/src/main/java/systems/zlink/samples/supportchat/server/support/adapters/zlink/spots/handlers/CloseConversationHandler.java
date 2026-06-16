package systems.zlink.samples.supportchat.server.support.adapters.zlink.spots.handlers;

import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.spots.ZLinkSpotActorRequestContext;
import systems.zlink.framework.spots.ZLinkSpotActorRequestHandler;
import systems.zlink.samples.supportchat.server.support.adapters.zlink.actors.SupportUserActor;
import systems.zlink.samples.supportchat.server.support.adapters.zlink.spots.ConversationSpot;
import systems.zlink.samples.supportchat.shared.contracts.Messages;

public final class CloseConversationHandler
    implements ZLinkSpotActorRequestHandler<
        ConversationSpot,
        SupportUserActor,
        Messages.CloseConversationReq,
        Messages.CloseConversationRes> {
    @Override
    public Messages.CloseConversationRes handle(
        ConversationSpot spot,
        SupportUserActor actor,
        ZLinkSpotActorRequestContext context,
        Messages.CloseConversationReq request,
        CancellationToken cancellationToken) {
        return spot.close(actor, request);
    }
}
