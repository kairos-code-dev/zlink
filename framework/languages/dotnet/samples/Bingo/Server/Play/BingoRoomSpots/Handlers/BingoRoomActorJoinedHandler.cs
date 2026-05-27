using Zlink.Framework.Contracts.Spots;
using Bingo.Server.Play.Actors;
using Microsoft.Extensions.Logging;

namespace Bingo.Server.Play.BingoRoomSpots.Handlers;

internal sealed class BingoRoomActorJoinedHandler(
    ILogger<BingoRoomActorJoinedHandler> logger)
    : IZLinkSpotPostActorJoinedHandler<BingoRoomSpot, PlayerActor>
{
    public ValueTask HandleAsync(
        BingoRoomSpot spot,
        PlayerActor actor,
        ZLinkSpotActorChangeResult info,
        CancellationToken cancellationToken)
    {
        _ = cancellationToken;
        logger.LogInformation(
            "bingo room: actor joined. room={RoomId}, actor={ActorId}, kind={Kind}",
            spot.Context.SpotRid.ToHex(),
            actor.ActorId,
            info.Kind);
        return ValueTask.CompletedTask;
    }
}
