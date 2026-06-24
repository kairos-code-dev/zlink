package systems.zlink.samples.supportchat.server.support.infrastructure.zlink.handlers;

import systems.zlink.contracts.core.RoutingId;
import systems.zlink.framework.channels.ZLinkRequestContext;
import systems.zlink.framework.channels.ZLinkRequestHandler;
import systems.zlink.framework.handlers.ZLinkHandlerGroup;
import systems.zlink.samples.supportchat.server.configuration.SampleTimings;
import systems.zlink.samples.supportchat.server.support.infrastructure.zlink.actors.SupportActorDirectory;
import systems.zlink.samples.supportchat.server.support.application.assignment.AgentAssignmentService;
import systems.zlink.samples.supportchat.server.support.application.assignment.AvailableAgent;
import systems.zlink.samples.supportchat.server.support.domain.conversation.ConversationModels.Statuses;
import systems.zlink.samples.supportchat.shared.contracts.Messages;

@ZLinkHandlerGroup("support")
public final class AssignAgentHandler
    implements ZLinkRequestHandler<
        Messages.AssignAgentReq,
        Messages.AssignAgentRes> {
    private final AgentAssignmentService assignment;
    private final SupportActorDirectory actors;

    public AssignAgentHandler(
        AgentAssignmentService assignment,
        SupportActorDirectory actors) {
        this.assignment = assignment;
        this.actors = actors;
    }

    @Override
    public Messages.AssignAgentRes handle(
        Messages.AssignAgentReq request,
        ZLinkRequestContext context) {
        AvailableAgent assigned = assignment.assignNextAgent();
        if (assigned == null) {
            return new Messages.AssignAgentRes(
                request.conversationId(),
                Statuses.WaitingForAgent,
                null);
        }
        SupportActorDirectory.Entry actor = actors.get(assigned.actorId());
        var joined = actor.actor()
            .context()
            .joinSpot(
                RoutingId.from(request.conversationId()),
                new Messages.JoinConversationReq(request.conversationId()))
            .timeout(SampleTimings.RequestTimeout)
            .await(Messages.JoinConversationRes.class);
        Messages.ConversationState state = joined.reply().state();
        return new Messages.AssignAgentRes(
            request.conversationId(),
            state.status(),
            actor.ref().actorId());
    }
}
