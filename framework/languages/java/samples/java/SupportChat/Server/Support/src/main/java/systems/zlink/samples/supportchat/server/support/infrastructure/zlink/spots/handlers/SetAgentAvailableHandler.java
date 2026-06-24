package systems.zlink.samples.supportchat.server.support.infrastructure.zlink.spots.handlers;

import static systems.zlink.framework.ZLinkAwait.await;

import systems.zlink.framework.CancellationToken;
import systems.zlink.framework.actors.ZLinkActorManager;
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
    private final ZLinkActorManager actorManager;

    public SetAgentAvailableHandler(
        AgentAvailabilityDirectory availability,
        SupportActorDirectory actors,
        ZLinkActorManager actorManager) {
        this.availability = availability;
        this.actors = actors;
        this.actorManager = actorManager;
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
        var ref = await(actorManager.find(actor.actorId()))
            .orElseThrow(() -> new IllegalStateException("Support actor ref is not available."));
        actors.addOrUpdate(actor, ref);
        availability.setAvailable(actor.actorId(), actor.displayName(), request.isAvailable());
        return new Messages.SetAgentAvailableRes(request.isAvailable());
    }
}
