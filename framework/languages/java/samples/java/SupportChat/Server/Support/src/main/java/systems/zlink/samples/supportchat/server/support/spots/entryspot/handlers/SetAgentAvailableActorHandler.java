package systems.zlink.samples.supportchat.server.support.spots.entryspot.handlers;

import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.spots.ZLinkEntrySpotActorRequestHandler;
import systems.zlink.framework.spots.ZLinkSpotActorRequestContext;
import systems.zlink.samples.supportchat.server.support.actors.SupportActorDirectory;
import systems.zlink.samples.supportchat.server.support.actors.SupportUserActor;
import systems.zlink.samples.supportchat.server.support.domain.ConversationStore;
import systems.zlink.samples.supportchat.server.support.spots.entryspot.SupportEntrySpot;
import systems.zlink.samples.supportchat.shared.contracts.Messages;

public final class SetAgentAvailableActorHandler
    implements ZLinkEntrySpotActorRequestHandler<
        SupportEntrySpot,
        SupportUserActor,
        Messages.SetAgentAvailableReq,
        Messages.SetAgentAvailableRes> {
    private final ConversationStore store;
    private final SupportActorDirectory directory;

    public SetAgentAvailableActorHandler(ConversationStore store, SupportActorDirectory directory) {
        this.store = store;
        this.directory = directory;
    }

    @Override
    public Messages.SetAgentAvailableRes handle(
        SupportEntrySpot spot,
        SupportUserActor actor,
        ZLinkSpotActorRequestContext context,
        Messages.SetAgentAvailableReq request,
        CancellationToken cancellationToken) {
        requireRole(actor, "agent");
        directory.remember(actor);
        return store.setAgentAvailable(request.isAvailable());
    }

    private static void requireRole(SupportUserActor actor, String expectedRole) {
        if (!expectedRole.equals(actor.role())) {
            throw new IllegalStateException("Expected role " + expectedRole + " but got " + actor.role());
        }
    }
}
