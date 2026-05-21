using Microsoft.Extensions.Logging;
using TicTacToe.SessionActorDispatch.Play;
using TicTacToe.SessionGateway.Play.EntrySpot;
using Zlink.Framework.Contracts.Handlers;
using Zlink.Framework.Contracts.Spots;

namespace TicTacToe.SessionGateway.Play.EntrySpot.Handlers;

internal sealed class TicTacToeEntrySpotActorLeftHandler(
    ILogger<TicTacToeEntrySpotActorLeftHandler> logger)
{
    [ZLinkSpotActorLeft]
    public ValueTask HandleAsync(
        TicTacToeEntrySpot entrySpot,
        PlayerActor actor,
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
