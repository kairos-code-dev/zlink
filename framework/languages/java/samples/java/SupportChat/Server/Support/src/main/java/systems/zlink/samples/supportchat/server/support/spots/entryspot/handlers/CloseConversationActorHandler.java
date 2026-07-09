package systems.zlink.samples.supportchat.server.support.spots.entryspot.handlers;

import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.spots.ZLinkEntrySpotActorRequestHandler;
import systems.zlink.framework.spots.ZLinkSpotActorRequestContext;
import systems.zlink.samples.supportchat.server.support.actors.SupportActorDirectory;
import systems.zlink.samples.supportchat.server.support.actors.SupportUserActor;
import systems.zlink.samples.supportchat.server.support.domain.ConversationStore;
import systems.zlink.samples.supportchat.server.support.spots.entryspot.SupportEntrySpot;
import systems.zlink.samples.supportchat.shared.contracts.Messages;

public final class CloseConversationActorHandler
    implements ZLinkEntrySpotActorRequestHandler<
        SupportEntrySpot,
        SupportUserActor,
        Messages.CloseConversationReq,
        Messages.CloseConversationRes> {
    private final ConversationStore store;
    private final SupportActorDirectory directory;

    public CloseConversationActorHandler(ConversationStore store, SupportActorDirectory directory) {
        this.store = store;
        this.directory = directory;
    }

    @Override
    public Messages.CloseConversationRes handle(
        SupportEntrySpot spot,
        SupportUserActor actor,
        ZLinkSpotActorRequestContext context,
        Messages.CloseConversationReq request,
        CancellationToken cancellationToken) {
        Messages.CloseConversationRes response = store.close(new Messages.CloseConversationSupportReq(
            request.conversationId(),
            actor.actorId(),
            request.reason()));
        Messages.ConversationClosedNotify notification =
            new Messages.ConversationClosedNotify(request.conversationId(), response.state());
        for (SupportUserActor participant : directory.participants(request.conversationId())) {
            participant.push(notification);
        }
        return response;
    }
}
