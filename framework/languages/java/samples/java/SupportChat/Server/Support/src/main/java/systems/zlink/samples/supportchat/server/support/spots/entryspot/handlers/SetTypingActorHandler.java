package systems.zlink.samples.supportchat.server.support.spots.entryspot.handlers;

import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.spots.ZLinkEntrySpotActorSendHandler;
import systems.zlink.framework.spots.ZLinkSpotActorSendContext;
import systems.zlink.samples.supportchat.server.support.actors.SupportActorDirectory;
import systems.zlink.samples.supportchat.server.support.actors.SupportUserActor;
import systems.zlink.samples.supportchat.server.support.domain.ConversationStore;
import systems.zlink.samples.supportchat.server.support.spots.entryspot.SupportEntrySpot;
import systems.zlink.samples.supportchat.shared.contracts.Messages;

public final class SetTypingActorHandler
    implements ZLinkEntrySpotActorSendHandler<
        SupportEntrySpot,
        SupportUserActor,
        Messages.SetTypingReq> {
    private final ConversationStore store;
    private final SupportActorDirectory directory;

    public SetTypingActorHandler(ConversationStore store, SupportActorDirectory directory) {
        this.store = store;
        this.directory = directory;
    }

    @Override
    public void handle(
        SupportEntrySpot spot,
        SupportUserActor actor,
        ZLinkSpotActorSendContext context,
        Messages.SetTypingReq request,
        CancellationToken cancellationToken) {
        Messages.ConversationState state = store.typing(new Messages.SetTypingSupportReq(
            request.conversationId(),
            actor.actorId(),
            request.isTyping()));
        Messages.TypingChangedNotify notification = new Messages.TypingChangedNotify(
            request.conversationId(),
            actor.actorId(),
            request.isTyping(),
            state);
        for (SupportUserActor participant : directory.participants(request.conversationId())) {
            if (!participant.actorId().equals(actor.actorId())) {
                participant.push(notification);
            }
        }
    }
}
