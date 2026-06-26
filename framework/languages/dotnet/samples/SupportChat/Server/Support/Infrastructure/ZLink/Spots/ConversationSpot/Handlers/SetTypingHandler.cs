using SupportChat.Server.Support.Infrastructure.ZLink.Actors;
using SupportChat.Shared.Contracts;
using Zlink.Framework.Contracts.Spots;

namespace SupportChat.Server.Support.Infrastructure.ZLink.Spots.ConversationSpot.Handlers;

internal sealed class SetTypingHandler
    : IZLinkSpotActorRequestHandler<ConversationSpot, SupportUserActor, SetTypingReq, SetTypingRes>
{
    public async ValueTask<SetTypingRes> HandleAsync(
        ConversationSpot spot,
        SupportUserActor actor,
        ZLinkSpotActorRequestContext context,
        SetTypingReq message,
        CancellationToken cancellationToken)
    {
        _ = context;
        return await spot.SetTypingAsync(actor, message, cancellationToken);
    }
}
