using SupportChat.Server.Support.Infrastructure.ZLink.Actors;
using SupportChat.Shared.Contracts;
using Zlink.Framework.Contracts.Spots;

namespace SupportChat.Server.Support.Infrastructure.ZLink.Spots.Handlers;

internal sealed class CloseConversationHandler
    : IZLinkSpotActorRequestHandler<ConversationSpot, SupportUserActor, CloseConversationReq, CloseConversationRes>
{
    public async ValueTask<CloseConversationRes> HandleAsync(
        ConversationSpot spot,
        SupportUserActor actor,
        ZLinkSpotActorRequestContext context,
        CloseConversationReq message,
        CancellationToken cancellationToken)
    {
        _ = context;
        return await spot.CloseAsync(actor, message, cancellationToken);
    }
}
