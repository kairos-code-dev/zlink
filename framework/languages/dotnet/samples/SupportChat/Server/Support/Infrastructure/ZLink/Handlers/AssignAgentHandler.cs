using SupportChat.Server.Configuration;
using SupportChat.Server.Support.Application.ConversationAssignment;
using SupportChat.Shared.Contracts;
using Microsoft.Extensions.Logging;
using Zlink.Framework.Contracts.Handlers;

namespace SupportChat.Server.Support.Infrastructure.ZLink.Handlers;

[ZLinkHandlerGroup("support")]
internal sealed class AssignAgentHandler(
    AgentAssignmentService assignment,
    ILogger<AssignAgentHandler> logger)
    : IZLinkRequestHandler<AssignAgentReq, AssignAgentRes>
{
    public ValueTask<AssignAgentRes> HandleAsync(
        AssignAgentReq request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        _ = context;
        logger.LogInformation(
            "support assign: request conversation={ConversationId} requested={RequestedAgentActorId}",
            request.ConversationId,
            request.RequestedAgentActorId);
        var assigned = assignment.AssignNextAgent();
        if (assigned is null)
            return ValueTask.FromResult(new AssignAgentRes(
                request.ConversationId,
                ConversationStatuses.WaitingForAgent,
                null));

        _ = cancellationToken;
        logger.LogInformation(
            "support assign: assigned conversation={ConversationId} agent={AgentActorId}",
            request.ConversationId,
            assigned.ActorId);
        return ValueTask.FromResult(new AssignAgentRes(
            request.ConversationId,
            ConversationStatuses.Active,
            assigned.ActorId));
    }
}
