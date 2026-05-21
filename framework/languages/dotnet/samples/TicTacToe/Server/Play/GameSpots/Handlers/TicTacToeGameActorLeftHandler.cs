using TicTacToe.Server.Play.Actors;

namespace TicTacToe.Server.Play.GameSpots.Handlers;

internal sealed class TicTacToeGameActorLeftHandler(ILogger<TicTacToeGameActorLeftHandler> logger)
{
    [ZLinkSpotActorLeft]
    public ValueTask HandleAsync(
        TicTacToeGame spot,
        PlayActor actor,
        ZLinkSpotActorLifecycleInfo info,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        logger.LogInformation(
            "game spot: actor left. actor={ActorId}, gameId={GameId}, epoch={CommitEpoch}",
            actor.ActorId,
            spot.Context.SpotRid.ToHex(),
            info.CommitEpoch);
        return ValueTask.CompletedTask;
    }
}
