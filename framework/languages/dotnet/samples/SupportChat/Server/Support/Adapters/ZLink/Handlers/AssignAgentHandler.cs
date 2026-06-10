using Systems.Zlink;
using Systems.Zlink.Codecs.Json;
using SupportChat.Server.Support.Adapters.ZLink.Actors;
using SupportChat.Server.Support.Application.ConversationAssignment;
using SupportChat.Shared.Configuration;
using SupportChat.Shared.Contracts;
using Zlink.Framework.Contracts.Handlers;

namespace SupportChat.Server.Support.Adapters.ZLink.Handlers;

[ZLinkHandlerGroup("support")]
internal sealed class AssignAgentHandler(
    AgentAssignmentService assignment,
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
        var conversationRid = RoutingId.FromHex(request.ConversationId);
        var joined = await actor.Context.JoinSpot(
                conversationRid,
                new JoinConversationReq(request.ConversationId).Encode())
            .SubmitAsync(cancellationToken);
        var state = joined.Reply.Decode<JoinConversationRes>().State;
        Console.Error.WriteLine($"support assign: joined conversation={request.ConversationId} agent={actor.ActorId} status={state.Status}");
        return new AssignAgentRes(
            request.ConversationId,
            state.Status,
            actor.ActorId);
    }
}
