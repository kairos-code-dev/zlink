using Zlink.Framework.Contracts.Handlers;
using Zlink.Framework.Contracts.Spots;
using TicTacToe.Server.Play.Actors;

namespace TicTacToe.Server.Play.EntrySpot.Handlers;

internal sealed class PlayEntrySpotActorJoinedHandler(ILogger<PlayEntrySpotActorJoinedHandler> logger)
{
    [ZLinkSpotPostActorJoined]
    public ValueTask HandleAsync(
        PlayEntrySpot entrySpot,
        PlayActor actor,
        ZLinkSpotActorChangeResult info,
        CancellationToken cancellationToken)
    {
        _ = entrySpot;
        cancellationToken.ThrowIfCancellationRequested();
        logger.LogInformation(
            "entry spot: actor joined. actor={ActorId}, kind={Kind}",
            actor.ActorId,
            info.Kind);
        return ValueTask.CompletedTask;
    }
}
