using Systems.Zlink;
using SupportChat.Server.Support.Infrastructure.ZLink.Actors;
using SupportChat.Server.Support.Application.ConversationAssignment;
using SupportChat.Server.Configuration;
using SupportChat.Shared.Contracts;
using Zlink.Framework.Contracts.Actors;
using Zlink.Framework.Contracts.Handlers;

namespace SupportChat.Server.Support.Infrastructure.ZLink.Handlers;

[ZLinkHandlerGroup("support")]
internal sealed class AssignAgentHandler(
    AgentAssignmentService assignment,
    IZLinkActorGateway actorGateway,
    SupportActorDirectory actors)
    : IZLinkRequestHandler<AssignAgentReq, AssignAgentRes>
{
    public async ValueTask<AssignAgentRes> HandleAsync(
        AssignAgentReq request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        _ = context;
        Console.Error.WriteLine($"support assign: request conversation={request.ConversationId} requested={request.RequestedAgentActorId}");
        var assigned = assignment.AssignNextAgent();
        if (assigned is null)
        {
            return new AssignAgentRes(
                request.ConversationId,
                ConversationStatuses.WaitingForAgent,
                AgentActorId: null);
        }

        var actor = actors.Get(assigned.ActorId);
        var conversationRid = RoutingId.From(request.ConversationId);
        var joined = await actorGateway.JoinSpot(
                actor.Ref,
                conversationRid,
                new JoinConversationReq(request.ConversationId))
            .Async(cancellationToken);
        var state = joined.Reply.Decode<JoinConversationRes>().State;
        Console.Error.WriteLine($"support assign: joined conversation={request.ConversationId} agent={actor.Ref.ActorId} status={state.Status}");
        return new AssignAgentRes(
            request.ConversationId,
            state.Status,
            actor.Ref.ActorId);
    }
}
