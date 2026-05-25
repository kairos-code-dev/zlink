using Microsoft.Extensions.Logging;
using TicTacToe.SessionGateway.Shared.Actors;
using TicTacToe.SessionGateway.Server.Play.GameSpots;
using Zlink.Framework.Contracts.Handlers;
using Zlink.Framework.Contracts.Spots;

namespace TicTacToe.SessionGateway.Server.Play.GameSpots.Handlers;

internal sealed class TicTacToeGameSpotActorLeftHandler(
    ILogger<TicTacToeGameSpotActorLeftHandler> logger)
{
    [ZLinkSpotActorLeft]
    public ValueTask HandleAsync(
        TicTacToeGameSpot spot,
        PlayerActor actor,
        ZLinkSpotActorLifecycleContext info,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        logger.LogInformation(
            "game spot: actor left. actor={ActorId}, matchId={MatchId}, epoch={CommitEpoch}",
            actor.ActorId,
            spot.Context.SpotRid.ToHex(),
            info.CommitEpoch);
        return ValueTask.CompletedTask;
    }
}
