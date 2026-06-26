using Bingo.Server.Play.Infrastructure.ZLink.Actors;
using Bingo.Server.Play.Domain.Bingo;
using Bingo.Server.Play.Infrastructure.ZLink.Spots.BingoRoomSpot;
using Bingo.Shared.Contracts;
using Zlink.Framework.Contracts.Handlers;
using Zlink.Framework.Contracts.Spots;

namespace Bingo.Server.Play.Infrastructure.ZLink.Spots.BingoRoomSpot.Handlers;

[ZLinkSpotActorRequestHandler(nameof(SubmitBingoCardReq))]
internal sealed class SubmitBingoCardHandler
    : IZLinkSpotActorRequestHandler<BingoRoom, PlayerActor, SubmitBingoCardReq, SubmitBingoCardRes>
{
    public async ValueTask<SubmitBingoCardRes> HandleAsync(
        BingoRoom spot,
        PlayerActor actor,
        ZLinkSpotActorRequestContext context,
        SubmitBingoCardReq message,
        CancellationToken cancellationToken)
    {
        _ = context;
        spot.EnsureRoomId(message.RoomId);
        var card = BingoCard.FromSubmittedNumbers(message.Card);
        var change = spot.SubmitCard(actor.ActorId, card);
        await spot.PublishAsync(change, cancellationToken);

        return new SubmitBingoCardRes { State = change.State };
    }
}
