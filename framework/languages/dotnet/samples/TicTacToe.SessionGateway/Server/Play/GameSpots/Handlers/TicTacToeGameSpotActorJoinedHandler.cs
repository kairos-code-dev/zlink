using Microsoft.Extensions.Logging;
using TicTacToe.SessionGateway.Server.Play.GameSpots;
using TicTacToe.SessionGateway.Shared.Actors;
using Zlink.Framework.Contracts.Handlers;
using Zlink.Framework.Contracts.Spots;

namespace TicTacToe.SessionGateway.Play.GameSpots.Handlers;

internal sealed class TicTacToeGameSpotActorJoinedHandler(
    ILogger<TicTacToeGameSpotActorJoinedHandler> logger)
{
    [ZLinkSpotPostActorJoined]
    public ValueTask HandleAsync(
        TicTacToeGameSpot spot,
        PlayerActor actor,
        ZLinkSpotActorChangeResult info,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        logger.LogInformation(
            "game spot: actor joined. actor={ActorId}, matchId={MatchId}, kind={Kind}",
            actor.ActorId,
            spot.Context.SpotRid.ToHex(),
            info.Kind);
        return ValueTask.CompletedTask;
    }
}
