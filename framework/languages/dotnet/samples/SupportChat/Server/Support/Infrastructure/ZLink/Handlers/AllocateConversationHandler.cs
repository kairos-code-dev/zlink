using SupportChat.Server.Configuration;
using SupportChat.Server.Support.Application.ConversationAssignment;
using SupportChat.Shared.Contracts;
using Microsoft.Extensions.Logging;
using Zlink.Framework.Contracts.Handlers;

namespace SupportChat.Server.Support.Infrastructure.ZLink.Handlers;

[ZLinkHandlerGroup("support")]
internal sealed class AllocateConversationHandler(
    SupportConversationAllocator allocator,
    ILogger<AllocateConversationHandler> logger)
    : IZLinkRequestHandler<AllocateConversationReq, AllocateConversationRes>
{
    public async ValueTask<AllocateConversationRes> HandleAsync(
        AllocateConversationReq request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        logger.LogInformation(
            "support allocate: request customer={CustomerActorId} subject={Subject}",
            request.CustomerActorId,
            request.Subject);
        var conversationId = await allocator.AllocateAsync(
            request.CustomerActorId,
            request.CustomerDisplayName,
            request.Subject,
            cancellationToken);
        logger.LogInformation("support allocate: created conversation={ConversationId}", conversationId);
        return new AllocateConversationRes(
            conversationId,
            ConversationStatuses.WaitingForAgent);
    }
}
