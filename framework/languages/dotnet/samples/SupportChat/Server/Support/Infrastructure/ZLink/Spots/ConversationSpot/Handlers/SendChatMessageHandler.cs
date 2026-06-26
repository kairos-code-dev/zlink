using SupportChat.Server.Support.Infrastructure.ZLink.Actors;
using SupportChat.Shared.Contracts;
using Zlink.Framework.Contracts.Spots;

namespace SupportChat.Server.Support.Infrastructure.ZLink.Spots.ConversationSpot.Handlers;

internal sealed class SendChatMessageHandler
    : IZLinkSpotActorRequestHandler<ConversationSpot, SupportUserActor, SendChatMessageReq, SendChatMessageRes>
{
    public async ValueTask<SendChatMessageRes> HandleAsync(
        ConversationSpot spot,
        SupportUserActor actor,
        ZLinkSpotActorRequestContext context,
        SendChatMessageReq message,
        CancellationToken cancellationToken)
    {
        _ = context;
        return await spot.SendMessageAsync(actor, message, cancellationToken);
    }
}
