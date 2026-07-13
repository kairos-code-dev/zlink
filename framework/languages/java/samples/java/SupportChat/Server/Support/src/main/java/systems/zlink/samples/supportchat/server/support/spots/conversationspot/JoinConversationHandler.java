package systems.zlink.samples.supportchat.server.support.spots.conversationspot;

import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import systems.zlink.framework.spots.ZLinkSpotActorRequestContext;
import systems.zlink.framework.spots.ZLinkSpotActorRequestHandler;
import systems.zlink.samples.supportchat.server.support.actors.SupportUserActor;
import systems.zlink.samples.supportchat.shared.contracts.Messages;

public final class JoinConversationHandler
    implements ZLinkSpotActorRequestHandler<ConversationSpot, SupportUserActor,
        Messages.JoinConversationReq, Messages.JoinConversationRes> {
    @Override
    public CompletionStage<Messages.JoinConversationRes> handle(
        ConversationSpot spot,
        SupportUserActor actor,
        ZLinkSpotActorRequestContext context,
        Messages.JoinConversationReq request) {
        return CompletableFuture.completedFuture(spot.refreshMembership(actor));
    }
}
