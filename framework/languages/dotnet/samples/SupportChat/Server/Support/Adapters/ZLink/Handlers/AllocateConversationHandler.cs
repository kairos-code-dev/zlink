using SupportChat.Server.Support.Application.ConversationAssignment;
using SupportChat.Server.Configuration;
using SupportChat.Shared.Contracts;
using Zlink.Framework.Contracts.Handlers;

namespace SupportChat.Server.Support.Adapters.ZLink.Handlers;

[ZLinkHandlerGroup("support")]
internal sealed class AllocateConversationHandler(SupportConversationAllocator allocator)
    : IZLinkRequestHandler<AllocateConversationReq, AllocateConversationRes>
{
    public async ValueTask<AllocateConversationRes> HandleAsync(
        AllocateConversationReq request,
        ZLinkRequestContext context,
        CancellationToken cancellationToken)
    {
        _ = context;
        Console.Error.WriteLine($"support allocate: request customer={request.CustomerActorId} subject={request.Subject}");
        var conversationId = await allocator.AllocateAsync(
                request.CustomerActorId,
                request.CustomerDisplayName,
                request.Subject,
                cancellationToken);
        Console.Error.WriteLine($"support allocate: created conversation={conversationId}");
        return new AllocateConversationRes(
            conversationId,
            ConversationStatuses.WaitingForAgent);
    }
}
