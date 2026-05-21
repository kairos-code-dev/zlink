using Microsoft.Extensions.Logging;

namespace Bingo.Server.Play.EntrySpot.Handlers;

internal sealed class BingoEntrySpotActorLeftHandler(
    ILogger<BingoEntrySpotActorLeftHandler> logger)
    : IZLinkEntrySpotActorLeftHandler<BingoEntrySpot, PlayerActor>
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
            "entry spot: actor left. actor={ActorId}, nextSpot={CurrentSpotRid}, epoch={CommitEpoch}",
            actor.ActorId,
            info.CurrentSpotRid?.ToHex(),
            info.CommitEpoch);
        return ValueTask.CompletedTask;
    }
}
