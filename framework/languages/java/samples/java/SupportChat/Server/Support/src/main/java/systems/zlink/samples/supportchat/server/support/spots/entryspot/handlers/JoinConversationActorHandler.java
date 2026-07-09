package systems.zlink.samples.supportchat.server.support.spots.entryspot.handlers;

import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.spots.ZLinkEntrySpotActorRequestHandler;
import systems.zlink.framework.spots.ZLinkSpotActorRequestContext;
import systems.zlink.samples.supportchat.server.support.actors.SupportActorDirectory;
import systems.zlink.samples.supportchat.server.support.actors.SupportUserActor;
import systems.zlink.samples.supportchat.server.support.domain.ConversationStore;
import systems.zlink.samples.supportchat.server.support.spots.entryspot.SupportEntrySpot;
import systems.zlink.samples.supportchat.shared.contracts.Messages;

public final class JoinConversationActorHandler
    implements ZLinkEntrySpotActorRequestHandler<
        SupportEntrySpot,
        SupportUserActor,
        Messages.JoinConversationReq,
        Messages.JoinConversationRes> {
    private final ConversationStore store;
    private final SupportActorDirectory directory;

    public JoinConversationActorHandler(ConversationStore store, SupportActorDirectory directory) {
        this.store = store;
        this.directory = directory;
    }

    @Override
    public Messages.JoinConversationRes handle(
        SupportEntrySpot spot,
        SupportUserActor actor,
        ZLinkSpotActorRequestContext context,
        Messages.JoinConversationReq request,
        CancellationToken cancellationToken) {
        Messages.JoinConversationRes joined = store.join(new Messages.JoinConversationSupportReq(
            request.conversationId(),
            actor.actorId(),
            actor.displayName(),
            actor.role()));
        directory.join(request.conversationId(), actor);
        Messages.ParticipantJoinedNotify notification = new Messages.ParticipantJoinedNotify(
            request.conversationId(),
            actor.actorId(),
            actor.role(),
            joined.state());
        for (SupportUserActor participant : directory.participants(request.conversationId())) {
            participant.push(notification);
        }
        return joined;
    }
}
