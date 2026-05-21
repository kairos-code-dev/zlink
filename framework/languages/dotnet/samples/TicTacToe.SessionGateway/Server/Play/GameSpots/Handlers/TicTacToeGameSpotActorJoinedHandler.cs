using Microsoft.Extensions.Logging;
using TicTacToe.SessionActorDispatch.Play;
using TicTacToe.SessionGateway.Play.GameSpots;
using Zlink.Framework.Contracts.Handlers;
using Zlink.Framework.Contracts.Spots;

namespace TicTacToe.SessionGateway.Play.GameSpots.Handlers;

internal sealed class TicTacToeGameSpotActorJoinedHandler(
    ILogger<TicTacToeGameSpotActorJoinedHandler> logger)
{
    [ZLinkSpotActorJoined]
    public ValueTask HandleAsync(
        TicTacToeGameSpot spot,
        PlayerActor actor,
        ZLinkSpotActorLifecycleInfo info,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        logger.LogInformation(
            "game spot: actor joined. actor={ActorId}, matchId={MatchId}, epoch={CommitEpoch}",
            actor.ActorId,
            spot.Context.SpotRid.ToHex(),
            info.CommitEpoch);
        return ValueTask.CompletedTask;
    }
}
