using Microsoft.Extensions.Logging;
using TicTacToe.SessionActorDispatch.Play;
using TicTacToe.SessionGateway.Play.EntrySpot;
using Zlink.Framework.Contracts.Handlers;
using Zlink.Framework.Contracts.Spots;

namespace TicTacToe.SessionGateway.Play.EntrySpot.Handlers;

internal sealed class TicTacToeEntrySpotActorJoinedHandler(
    ILogger<TicTacToeEntrySpotActorJoinedHandler> logger)
{
    [ZLinkSpotActorJoined]
    public ValueTask HandleAsync(
        TicTacToeEntrySpot entrySpot,
        PlayerActor actor,
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
