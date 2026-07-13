package systems.zlink.samples.supportchat.server.support.spots.entryspot.handlers;

import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.actors.ZLinkActorJoinResult;
import systems.zlink.framework.spots.ZLinkEntrySpotActorRequestHandler;
import systems.zlink.framework.spots.ZLinkSpotActorRequestContext;
import systems.zlink.samples.supportchat.server.configuration.SampleNames;
import systems.zlink.samples.supportchat.server.configuration.SampleTimings;
import systems.zlink.samples.supportchat.server.support.actors.SupportUserActor;
import systems.zlink.samples.supportchat.server.support.spots.entryspot.SupportEntrySpot;
import systems.zlink.samples.supportchat.shared.contracts.Messages;

public final class OpenConversationActorHandler
    implements ZLinkEntrySpotActorRequestHandler<
        SupportEntrySpot,
        SupportUserActor,
        Messages.OpenConversationReq,
        Messages.OpenConversationRes> {
    @Override
    public java.util.concurrent.CompletionStage<Messages.OpenConversationRes> handle(
        SupportEntrySpot spot,
        SupportUserActor actor,
        ZLinkSpotActorRequestContext context,
        Messages.OpenConversationReq request) {
        requireRole(actor, SampleNames.Roles.Customer);
        return spot.context().outbound()
            .requestToChannel(
                SampleNames.ApiChannel,
                new Messages.OpenConversationApiReq(actor.actorId(), actor.displayName(), request.subject()))
            .timeout(SampleTimings.RequestTimeout)
            .submit(Messages.OpenConversationApiRes.class)
            .thenCompose(opened -> actor.context()
                .joinSpot(
                    RoutingId.from(opened.conversationId()),
                    new Messages.JoinConversationReq(
                        actor.participantId(), actor.role(), actor.displayName()))
                .timeout(SampleTimings.RequestTimeout)
                .submit(Messages.JoinConversationRes.class)
                .thenApply(result -> {
                    if (!(result instanceof ZLinkActorJoinResult.Accepted<?> accepted)) {
                        throw new IllegalStateException("Customer conversation join was rejected");
                    }
                    Messages.JoinConversationRes joined =
                        (Messages.JoinConversationRes) accepted.reply();
                    return new Messages.OpenConversationRes(opened.conversationId(), joined.state());
                }));
    }

    private static void requireRole(SupportUserActor actor, String expectedRole) {
        if (!expectedRole.equals(actor.role())) {
            throw new IllegalStateException("Expected role " + expectedRole + " but got " + actor.role());
        }
    }
}
