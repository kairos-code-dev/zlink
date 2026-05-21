using Bingo.Shared.Contracts;

namespace Bingo.Server.Play.BingoRoomSpots.Handlers;

internal sealed class BingoRoomJoinHandler
    : IZLinkSpotActorJoinHandler<BingoRoomSpot, PlayerActor, BingoRoomJoinReq, BingoRoomJoinRes>
{
    public async ValueTask<BingoRoomJoinRes> HandleAsync(
        BingoRoomSpot spot,
        PlayerActor actor,
        BingoRoomJoinReq request,
        CancellationToken cancellationToken)
    {
        return await spot.JoinAsync(actor, request, cancellationToken).ConfigureAwait(false);
    }
}
