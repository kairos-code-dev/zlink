package systems.zlink.samples.supportchat.server.support.adapters.zlink.spots.handlers;

import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.spots.ZLinkSpotActorRequestContext;
import systems.zlink.framework.spots.ZLinkSpotActorRequestHandler;
import systems.zlink.samples.supportchat.server.support.adapters.zlink.actors.SupportUserActor;
import systems.zlink.samples.supportchat.server.support.adapters.zlink.spots.ConversationSpot;
import systems.zlink.samples.supportchat.shared.contracts.Messages;

public final class SetTypingHandler
    implements ZLinkSpotActorRequestHandler<
        ConversationSpot,
        SupportUserActor,
        Messages.SetTypingReq,
        Messages.SetTypingRes> {
    @Override
    public Messages.SetTypingRes handle(
        ConversationSpot spot,
        SupportUserActor actor,
        ZLinkSpotActorRequestContext context,
        Messages.SetTypingReq request,
        CancellationToken cancellationToken) {
        return spot.setTyping(actor, request);
    }
}
