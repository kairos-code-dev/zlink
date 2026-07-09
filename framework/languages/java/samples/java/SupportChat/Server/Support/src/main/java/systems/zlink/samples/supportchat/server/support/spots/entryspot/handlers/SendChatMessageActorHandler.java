package systems.zlink.samples.supportchat.server.support.spots.entryspot.handlers;

import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.spots.ZLinkEntrySpotActorRequestHandler;
import systems.zlink.framework.spots.ZLinkSpotActorRequestContext;
import systems.zlink.samples.supportchat.server.support.actors.SupportActorDirectory;
import systems.zlink.samples.supportchat.server.support.actors.SupportUserActor;
import systems.zlink.samples.supportchat.server.support.domain.ConversationStore;
import systems.zlink.samples.supportchat.server.support.spots.entryspot.SupportEntrySpot;
import systems.zlink.samples.supportchat.shared.contracts.Messages;

public final class SendChatMessageActorHandler
    implements ZLinkEntrySpotActorRequestHandler<
        SupportEntrySpot,
        SupportUserActor,
        Messages.SendChatMessageReq,
        Messages.SendChatMessageRes> {
    private final ConversationStore store;
    private final SupportActorDirectory directory;

    public SendChatMessageActorHandler(ConversationStore store, SupportActorDirectory directory) {
        this.store = store;
        this.directory = directory;
    }

    @Override
    public Messages.SendChatMessageRes handle(
        SupportEntrySpot spot,
        SupportUserActor actor,
        ZLinkSpotActorRequestContext context,
        Messages.SendChatMessageReq request,
        CancellationToken cancellationToken) {
        Messages.SendChatMessageRes response = store.append(new Messages.SendChatMessageSupportReq(
            request.conversationId(),
            actor.actorId(),
            request.text()));
        Messages.ChatMessageNotify notification =
            new Messages.ChatMessageNotify(request.conversationId(), response.message(), response.state());
        for (SupportUserActor participant : directory.participants(request.conversationId())) {
            if (!participant.actorId().equals(actor.actorId())) {
                participant.push(notification);
            }
        }
        return response;
    }
}
