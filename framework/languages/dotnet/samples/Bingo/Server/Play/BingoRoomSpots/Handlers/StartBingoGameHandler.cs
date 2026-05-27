using Zlink.Framework.Contracts.Spots;
using Bingo.Server.Play.Actors;
using Bingo.Shared.Contracts;

namespace Bingo.Server.Play.BingoRoomSpots.Handlers;

internal sealed class StartBingoGameHandler
    : IZLinkSpotActorRequestHandler<BingoRoomSpot, PlayerActor, StartBingoGameReq, StartBingoGameRes>
{
    public async ValueTask<StartBingoGameRes> HandleAsync(
        BingoRoomSpot spot,
        PlayerActor actor,
        ZLinkSpotActorRequestContext context,
        StartBingoGameReq message,
        CancellationToken cancellationToken)
    {
        _ = context;
        return await spot.StartAsync(actor, message, cancellationToken);
    }
}
