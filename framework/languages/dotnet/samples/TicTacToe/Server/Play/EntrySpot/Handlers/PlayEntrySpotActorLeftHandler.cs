using Zlink.Framework.Contracts.Handlers;
using Zlink.Framework.Contracts.Spots;
using TicTacToe.Server.Play.Actors;

namespace TicTacToe.Server.Play.EntrySpot.Handlers;

internal sealed class PlayEntrySpotActorLeftHandler(ILogger<PlayEntrySpotActorLeftHandler> logger)
{
    [ZLinkSpotActorLeft]
    public ValueTask HandleAsync(
        PlayEntrySpot entrySpot,
        PlayActor actor,
        ZLinkSpotActorChangeResult info,
        CancellationToken cancellationToken)
    {
        _ = entrySpot;
        cancellationToken.ThrowIfCancellationRequested();
        logger.LogInformation(
            "entry spot: actor left. actor={ActorId}, kind={Kind}",
            actor.ActorId,
            info.Kind);
        return ValueTask.CompletedTask;
    }
}
