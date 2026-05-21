using TicTacToe.Server.Play.Actors;

namespace TicTacToe.Server.Play.EntrySpot.Handlers;

internal sealed class PlayEntrySpotActorJoinedHandler(ILogger<PlayEntrySpotActorJoinedHandler> logger)
{
    [ZLinkSpotActorJoined]
    public ValueTask HandleAsync(
        PlayEntrySpot entrySpot,
        PlayActor actor,
        ZLinkSpotActorLifecycleInfo info,
        CancellationToken cancellationToken)
    {
        _ = entrySpot;
        cancellationToken.ThrowIfCancellationRequested();
        logger.LogInformation(
            "entry spot: actor joined. actor={ActorId}, epoch={CommitEpoch}",
            actor.ActorId,
            info.CommitEpoch);
        return ValueTask.CompletedTask;
    }
}
