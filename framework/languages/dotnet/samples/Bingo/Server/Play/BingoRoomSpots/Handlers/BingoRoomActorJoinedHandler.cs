using Microsoft.Extensions.Logging;

namespace Bingo.Server.Play.BingoRoomSpots.Handlers;

internal sealed class BingoRoomActorJoinedHandler(
    ILogger<BingoRoomActorJoinedHandler> logger)
    : IZLinkSpotActorJoinedHandler<BingoRoomSpot, PlayerActor>
{
    public ValueTask HandleAsync(
        BingoRoomSpot spot,
        PlayerActor actor,
        ZLinkSpotActorLifecycleInfo info,
        CancellationToken cancellationToken)
    {
        _ = cancellationToken;
        logger.LogInformation(
            "bingo room: actor joined. room={RoomId}, actor={ActorId}, previousSpot={PreviousSpotRid}, epoch={CommitEpoch}",
            spot.Context.SpotRid.ToHex(),
            actor.ActorId,
            info.PreviousSpotRid?.ToHex(),
            info.CommitEpoch);
        return ValueTask.CompletedTask;
    }
}
