using SupportChat.Server.Support.Application.ConversationAssignment;
using SupportChat.Server.Support.Infrastructure.ZLink.Spots;
using Systems.Zlink;
using Zlink.Framework.Contracts.Codecs.Json;
using Zlink.Framework.Contracts.Spots;

namespace SupportChat.Server.Support.Infrastructure.ZLink;

internal sealed class ConversationStarter(IZLinkSpotManager spots) : IConversationStarter
{
    public async ValueTask StartAsync(
        string conversationId,
        ConversationStartRequest request,
        CancellationToken cancellationToken)
    {
        await spots.GetOrCreateAsync<ConversationSpot>(
            RoutingId.From(conversationId),
            new ConversationCreateRequest(
                request.CustomerActorId,
                request.CustomerDisplayName,
                request.Subject,
                request.CreatedAtUnixMs),
            cancellationToken);
    }
}
