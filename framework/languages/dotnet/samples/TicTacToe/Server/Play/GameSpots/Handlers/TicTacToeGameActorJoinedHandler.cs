using TicTacToe.Server.Play.Actors;

namespace TicTacToe.Server.Play.GameSpots.Handlers;

internal sealed class TicTacToeGameActorJoinedHandler(ILogger<TicTacToeGameActorJoinedHandler> logger)
{
    [ZLinkSpotActorJoined]
    public ValueTask HandleAsync(
        TicTacToeGame spot,
        PlayActor actor,
        ZLinkSpotActorLifecycleInfo info,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        logger.LogInformation(
            "game spot: actor joined. actor={ActorId}, gameId={GameId}, epoch={CommitEpoch}",
            actor.ActorId,
            spot.Context.SpotRid.ToHex(),
            info.CommitEpoch);
        return ValueTask.CompletedTask;
    }
}
