using Zlink.Framework.Contracts.Handlers;
using Zlink.Framework.Contracts.Spots;
using TicTacToe.Server.Play.Actors;

namespace TicTacToe.Server.Play.GameSpots.Handlers;

internal sealed class TicTacToeGameActorLeftHandler(ILogger<TicTacToeGameActorLeftHandler> logger)
{
    [ZLinkSpotActorLeft]
    public ValueTask HandleAsync(
        TicTacToeGame spot,
        PlayActor actor,
        ZLinkSpotActorChangeResult info,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        logger.LogInformation(
            "game spot: actor left. actor={ActorId}, gameId={GameId}, kind={Kind}",
            actor.ActorId,
            spot.Context.SpotRid.ToHex(),
            info.Kind);
        return ValueTask.CompletedTask;
    }
}
