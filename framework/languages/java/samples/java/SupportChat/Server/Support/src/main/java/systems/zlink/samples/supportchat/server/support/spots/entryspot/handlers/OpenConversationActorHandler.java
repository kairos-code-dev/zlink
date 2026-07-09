package systems.zlink.samples.supportchat.server.support.spots.entryspot.handlers;

import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.spots.ZLinkEntrySpotActorRequestHandler;
import systems.zlink.framework.spots.ZLinkSpotActorRequestContext;
import systems.zlink.samples.supportchat.server.configuration.SampleNames;
import systems.zlink.samples.supportchat.server.configuration.SampleTimings;
import systems.zlink.samples.supportchat.server.support.actors.SupportActorDirectory;
import systems.zlink.samples.supportchat.server.support.actors.SupportUserActor;
import systems.zlink.samples.supportchat.server.support.domain.ConversationStore;
import systems.zlink.samples.supportchat.server.support.spots.entryspot.SupportEntrySpot;
import systems.zlink.samples.supportchat.shared.contracts.Messages;

public final class OpenConversationActorHandler
    implements ZLinkEntrySpotActorRequestHandler<
        SupportEntrySpot,
        SupportUserActor,
        Messages.OpenConversationReq,
        Messages.OpenConversationRes> {
    private final ConversationStore store;
    private final SupportActorDirectory directory;

    public OpenConversationActorHandler(ConversationStore store, SupportActorDirectory directory) {
        this.store = store;
        this.directory = directory;
    }

    @Override
    public Messages.OpenConversationRes handle(
        SupportEntrySpot spot,
        SupportUserActor actor,
        ZLinkSpotActorRequestContext context,
        Messages.OpenConversationReq request,
        CancellationToken cancellationToken) {
        requireRole(actor, "customer");
        Messages.OpenConversationApiRes opened = spot.context().outbound()
            .requestToChannel(
                SampleNames.ApiChannel,
                new Messages.OpenConversationApiReq(actor.actorId(), actor.displayName(), request.subject()))
            .timeout(SampleTimings.RequestTimeout)
            .await(Messages.OpenConversationApiRes.class);
        Messages.JoinConversationRes joined = store.join(new Messages.JoinConversationSupportReq(
            opened.conversationId(),
            actor.actorId(),
            actor.displayName(),
            actor.role()));
        directory.join(opened.conversationId(), actor);
        actor.push(new Messages.ParticipantJoinedNotify(
            opened.conversationId(),
            actor.actorId(),
            actor.role(),
            joined.state()));
        actor.push(new Messages.ConversationAssignedNotify(opened.conversationId(), joined.state()));
        return new Messages.OpenConversationRes(opened.conversationId(), joined.state());
    }

    private static void requireRole(SupportUserActor actor, String expectedRole) {
        if (!expectedRole.equals(actor.role())) {
            throw new IllegalStateException("Expected role " + expectedRole + " but got " + actor.role());
        }
    }
}
