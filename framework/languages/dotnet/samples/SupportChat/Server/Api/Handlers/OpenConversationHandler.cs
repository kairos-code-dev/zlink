using SupportChat.Server.Configuration;
using SupportChat.Shared.Contracts;
using Microsoft.Extensions.Logging;
using Zlink.Framework.Contracts.Channels;
using Zlink.Framework.Contracts.Handlers;

namespace SupportChat.Server.Api.Handlers;

[ZLinkHandlerGroup("api")]
internal sealed class OpenConversationHandler(
    IZLinkRouteClient channels,
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
        var allocated = await channels.RequestToChannel(SampleNames.SupportChannel, SampleNames.SupportChannel,
                new AllocateConversationReq(
                    request.CustomerActorId,
                    request.CustomerDisplayName,
                    request.Subject))
            .Async<AllocateConversationRes>(cancellationToken);
        logger.LogInformation(
            "support api open: allocated conversation={ConversationId} status={Status}",
            allocated.ConversationId,
            allocated.Status);

        // Agent assignment is a separate step driven after the customer has joined
        // the conversation (see OpenConversationActorHandler), so the open response
        // carries only the allocation result.
        return new OpenConversationApiRes(
            allocated.ConversationId,
            allocated.Status);
    }
}
