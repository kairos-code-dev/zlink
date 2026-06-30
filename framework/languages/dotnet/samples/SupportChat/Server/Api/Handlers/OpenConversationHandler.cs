using SupportChat.Server.Configuration;
using SupportChat.Shared.Contracts;
using Microsoft.Extensions.Logging;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Handlers;

namespace SupportChat.Server.Api.Handlers;

[ZLinkHandlerGroup("api")]
internal sealed class OpenConversationHandler(
    IZLinkChannelClient channels,
    ILogger<OpenConversationHandler> logger)
    : IZLinkRequestHandler<OpenConversationApiReq, OpenConversationApiRes>
{
    public async ValueTask<OpenConversationApiRes> HandleAsync(
        OpenConversationApiReq request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        logger.LogInformation(
            "support api open: allocate request customer={CustomerActorId} subject={Subject}",
            request.CustomerActorId,
            request.Subject);
        var allocated = await channels.RequestToChannel(
                SampleNames.SupportChannel,
                new AllocateConversationReq(
                    request.CustomerActorId,
                    request.CustomerDisplayName,
                    request.Subject))
            .Async<AllocateConversationRes>(cancellationToken);
        logger.LogInformation(
            "support api open: allocated conversation={ConversationId} status={Status}",
            allocated.ConversationId,
            allocated.Status);

        var assigned = await channels.RequestToChannel(
                SampleNames.SupportChannel,
                new AssignAgentReq(
                    allocated.ConversationId,
                    null))
            .Async<AssignAgentRes>(cancellationToken);
        logger.LogInformation(
            "support api open: assigned conversation={ConversationId} status={Status} agent={AgentActorId}",
            assigned.ConversationId,
            assigned.Status,
            assigned.AgentActorId);

        return new OpenConversationApiRes(
            allocated.ConversationId,
            assigned.Status,
            assigned.AgentActorId);
    }
}
