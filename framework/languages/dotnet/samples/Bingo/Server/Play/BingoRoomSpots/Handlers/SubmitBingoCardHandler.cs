using Bingo.Server.Play.Actors;
using Bingo.Shared.Contracts;
using Zlink.Framework.Contracts.Spots;

namespace Bingo.Server.Play.BingoRoomSpots.Handlers;

internal sealed class SubmitBingoCardHandler
    : IZLinkSpotActorRequestHandler<BingoRoomSpot, PlayerActor, SubmitBingoCardReq, SubmitBingoCardRes>
{
    public async ValueTask<SubmitBingoCardRes> HandleAsync(
        BingoRoomSpot spot,
        PlayerActor actor,
        ZLinkSpotActorRequestContext context,
        SubmitBingoCardReq message,
        CancellationToken cancellationToken)
    {
        _ = context;
        spot.EnsureRoomId(message.RoomId);
        var card = BingoCard.FromSubmittedNumbers(message.Card);
        var change = spot.SubmitCard(actor.ActorId, card);
        if (change.ShouldStartDrawTimer)
        {
            await spot.StartDrawTimerAsync(cancellationToken);
        }

        return new SubmitBingoCardRes(change.State);
    }
}
