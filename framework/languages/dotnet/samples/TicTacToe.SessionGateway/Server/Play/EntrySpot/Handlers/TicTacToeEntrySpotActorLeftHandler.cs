using Microsoft.Extensions.Logging;
using TicTacToe.SessionGateway.Shared.Actors;
using TicTacToe.SessionGateway.Server.Play.EntrySpot;
using Zlink.Framework.Contracts.Handlers;
using Zlink.Framework.Contracts.Spots;

namespace TicTacToe.SessionGateway.Server.Play.EntrySpot.Handlers;

internal sealed class TicTacToeEntrySpotActorLeftHandler(
    ILogger<TicTacToeEntrySpotActorLeftHandler> logger)
{
    [ZLinkSpotActorLeft]
    public ValueTask HandleAsync(
        TicTacToeEntrySpot entrySpot,
        PlayerActor actor,
        ZLinkSpotActorLifecycleContext info,
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
