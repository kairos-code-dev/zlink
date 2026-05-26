using Microsoft.Extensions.Logging;
using TicTacToe.SessionGateway.Server.Play.EntrySpot;
using TicTacToe.SessionGateway.Shared.Actors;
using Zlink.Framework.Contracts.Handlers;
using Zlink.Framework.Contracts.Spots;

namespace TicTacToe.SessionGateway.Play.EntrySpot.Handlers;

internal sealed class TicTacToeEntrySpotActorJoinedHandler(
    ILogger<TicTacToeEntrySpotActorJoinedHandler> logger)
{
    [ZLinkSpotPostActorJoined]
    public ValueTask HandleAsync(
        TicTacToeEntrySpot entrySpot,
        PlayerActor actor,
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
