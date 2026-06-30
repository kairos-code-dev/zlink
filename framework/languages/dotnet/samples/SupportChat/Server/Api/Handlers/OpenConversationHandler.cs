using SupportChat.Server.Configuration;
using SupportChat.Shared.Contracts;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Handlers;

namespace SupportChat.Server.Api.Handlers;

[ZLinkHandlerGroup("api")]
internal sealed class OpenConversationHandler(IZLinkChannelClient channels)
    : IZLinkRequestHandler<OpenConversationApiReq, OpenConversationApiRes>
{
    public async ValueTask<OpenConversationApiRes> HandleAsync(
        OpenConversationApiReq request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        _ = context;
        Console.Error.WriteLine(
            $"support api open: allocate request customer={request.CustomerActorId} subject={request.Subject}");
        var allocated = await channels.RequestToChannel(
                SampleNames.SupportChannel,
                new AllocateConversationReq(
                    request.CustomerActorId,
                    request.CustomerDisplayName,
                    request.Subject))
            .Async<AllocateConversationRes>(cancellationToken);
        Console.Error.WriteLine(
            $"support api open: allocated conversation={allocated.ConversationId} status={allocated.Status}");

        var assigned = await channels.RequestToChannel(
                SampleNames.SupportChannel,
                new AssignAgentReq(
                    allocated.ConversationId,
                    null))
            .Async<AssignAgentRes>(cancellationToken);
        Console.Error.WriteLine(
            $"support api open: assigned conversation={assigned.ConversationId} status={assigned.Status} agent={assigned.AgentActorId}");

        return new OpenConversationApiRes(
            allocated.ConversationId,
            assigned.Status,
            assigned.AgentActorId);
    }
}