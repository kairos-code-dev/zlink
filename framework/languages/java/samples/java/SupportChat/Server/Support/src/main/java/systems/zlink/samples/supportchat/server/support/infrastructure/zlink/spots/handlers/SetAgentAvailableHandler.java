package systems.zlink.samples.supportchat.server.support.infrastructure.zlink.spots.handlers;

import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.spots.ZLinkEntrySpotActorRequestHandler;
import systems.zlink.framework.spots.ZLinkSpotActorRequestContext;
import systems.zlink.samples.supportchat.server.support.infrastructure.zlink.actors.SupportActorDirectory;
import systems.zlink.samples.supportchat.server.support.infrastructure.zlink.actors.SupportUserActor;
import systems.zlink.samples.supportchat.server.support.infrastructure.zlink.spots.SupportEntrySpot;
import systems.zlink.samples.supportchat.server.support.application.assignment.AgentAvailabilityDirectory;
import systems.zlink.samples.supportchat.server.support.domain.conversation.ConversationModels.Roles;
import systems.zlink.samples.supportchat.shared.contracts.Messages;

public final class SetAgentAvailableHandler
    implements ZLinkEntrySpotActorRequestHandler<
        SupportEntrySpot,
        SupportUserActor,
        Messages.SetAgentAvailableReq,
        Messages.SetAgentAvailableRes> {
    private final AgentAvailabilityDirectory availability;
    private final SupportActorDirectory actors;

    public SetAgentAvailableHandler(
        AgentAvailabilityDirectory availability,
        SupportActorDirectory actors) {
        this.availability = availability;
        this.actors = actors;
    }

    @Override
    public Messages.SetAgentAvailableRes handle(
        SupportEntrySpot entrySpot,
        SupportUserActor actor,
        ZLinkSpotActorRequestContext context,
        Messages.SetAgentAvailableReq request,
        CancellationToken cancellationToken) {
        if (!Roles.Agent.equals(actor.role())) {
            throw new IllegalStateException("Only agent actors can set availability.");
        }
        actors.addOrUpdate(actor);
        availability.setAvailable(actor.actorId(), actor.displayName(), request.isAvailable());
        return new Messages.SetAgentAvailableRes(request.isAvailable());
    }
}
