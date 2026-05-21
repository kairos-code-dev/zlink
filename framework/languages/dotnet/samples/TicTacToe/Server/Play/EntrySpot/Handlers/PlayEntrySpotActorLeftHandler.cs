using TicTacToe.Server.Play.Actors;

namespace TicTacToe.Server.Play.EntrySpot.Handlers;

internal sealed class PlayEntrySpotActorLeftHandler(ILogger<PlayEntrySpotActorLeftHandler> logger)
{
    [ZLinkSpotActorLeft]
    public ValueTask HandleAsync(
        PlayEntrySpot entrySpot,
        PlayActor actor,
        ZLinkSpotActorLifecycleInfo info,
        CancellationToken cancellationToken)
    {
        _ = entrySpot;
        cancellationToken.ThrowIfCancellationRequested();
        logger.LogInformation(
            "entry spot: actor left. actor={ActorId}, epoch={CommitEpoch}",
            actor.ActorId,
            info.CommitEpoch);
        return ValueTask.CompletedTask;
    }
}
