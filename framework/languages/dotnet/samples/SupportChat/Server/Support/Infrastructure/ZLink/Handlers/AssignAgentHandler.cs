using SupportChat.Server.Support.Application.ConversationAssignment;
using SupportChat.Server.Configuration;
using SupportChat.Shared.Contracts;
using Zlink.Framework.Contracts.Handlers;

namespace SupportChat.Server.Support.Infrastructure.ZLink.Handlers;

[ZLinkHandlerGroup("support")]
internal sealed class AssignAgentHandler(
    AgentAssignmentService assignment)
    : IZLinkRequestHandler<AssignAgentReq, AssignAgentRes>
{
    public ValueTask<AssignAgentRes> HandleAsync(
        AssignAgentReq request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        _ = context;
        Console.Error.WriteLine($"support assign: request conversation={request.ConversationId} requested={request.RequestedAgentActorId}");
        var assigned = assignment.AssignNextAgent();
        if (assigned is null)
        {
            return ValueTask.FromResult(new AssignAgentRes(
                request.ConversationId,
                ConversationStatuses.WaitingForAgent,
                AgentActorId: null));
        }

        _ = cancellationToken;
        Console.Error.WriteLine($"support assign: assigned conversation={request.ConversationId} agent={assigned.ActorId}");
        return ValueTask.FromResult(new AssignAgentRes(
            request.ConversationId,
            ConversationStatuses.Active,
            assigned.ActorId));
    }
}
