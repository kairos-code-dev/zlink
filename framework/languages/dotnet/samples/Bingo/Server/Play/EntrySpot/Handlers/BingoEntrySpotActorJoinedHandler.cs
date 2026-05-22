using Bingo.Server.Play.Actors;
using Bingo.Server.Play.BingoRoomSpots;
using Microsoft.Extensions.Logging;

namespace Bingo.Server.Play.EntrySpot.Handlers;

internal sealed class BingoEntrySpotActorJoinedHandler(
    ILogger<BingoEntrySpotActorJoinedHandler> logger)
    : IZLinkEntrySpotActorJoinedHandler<BingoEntrySpot, PlayerActor>
{
    public ValueTask HandleAsync(
        BingoEntrySpot entrySpot,
        PlayerActor actor,
        ZLinkSpotActorLifecycleInfo info,
        CancellationToken cancellationToken)
    {
        _ = entrySpot;
        _ = cancellationToken;
        logger.LogInformation(
            "entry spot: actor joined. actor={ActorId}, currentSpot={CurrentSpotRid}, epoch={CommitEpoch}",
            actor.ActorId,
            info.CurrentSpotRid?.ToHex(),
            info.CommitEpoch);
        return ValueTask.CompletedTask;
    }
}
