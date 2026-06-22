package systems.zlink.samples.supportchat.server.support.infrastructure.zlink.spots.handlers;

import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.spots.ZLinkEntrySpotActorRequestHandler;
import systems.zlink.framework.spots.ZLinkSpotActorRequestContext;
import systems.zlink.samples.supportchat.server.configuration.SampleNames;
import systems.zlink.samples.supportchat.server.configuration.SampleTimings;
import systems.zlink.samples.supportchat.server.support.infrastructure.zlink.actors.SupportUserActor;
import systems.zlink.samples.supportchat.server.support.infrastructure.zlink.spots.SupportEntrySpot;
import systems.zlink.samples.supportchat.server.support.domain.conversation.ConversationModels.Roles;
import systems.zlink.samples.supportchat.shared.contracts.Messages;

public final class OpenConversationActorHandler
    implements ZLinkEntrySpotActorRequestHandler<
        SupportEntrySpot,
        SupportUserActor,
        Messages.OpenConversationReq,
        Messages.OpenConversationRes> {
    @Override
    public Messages.OpenConversationRes handle(
        SupportEntrySpot entrySpot,
        SupportUserActor actor,
        ZLinkSpotActorRequestContext context,
        Messages.OpenConversationReq request,
        CancellationToken cancellationToken) {
        if (!Roles.Customer.equals(actor.role())) {
            throw new IllegalStateException("Only customer actors can open a conversation.");
        }
        Messages.OpenConversationApiRes opened = entrySpot.context().outbound().requestToChannel(
                SampleNames.ApiChannel,
                new Messages.OpenConversationApiReq(
                    actor.actorId(),
                    actor.displayName(),
                    request.subject()))
            .timeout(SampleTimings.RequestTimeout)
            .await(Messages.OpenConversationApiRes.class);
        if (cancellationToken.isCancellationRequested()) {
            throw new IllegalStateException("OpenConversationReq was cancelled");
        }
        var joined = actor.context()
            .joinSpot(
                RoutingId.from(opened.conversationId()),
                new Messages.JoinConversationReq(opened.conversationId()))
            .timeout(SampleTimings.RequestTimeout)
            .await(Messages.JoinConversationRes.class);
        return new Messages.OpenConversationRes(opened.conversationId(), joined.reply().state());
    }
}
