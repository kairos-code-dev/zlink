using TicTacToe.Server.Play.Actors;

namespace TicTacToe.Server.Play.Games;

sealed class TicTacToeGameJoinHandler(ILogger<TicTacToeGameJoinHandler> logger)
    : IZLinkSpotActorJoinHandler<TicTacToeGame, PlayActor, TicTacToeGameJoinReq, TicTacToeGameJoinRes>
{
    public async ValueTask<TicTacToeGameJoinRes> HandleAsync(
        TicTacToeGame spot,
        PlayActor player,
        TicTacToeGameJoinReq request,
        CancellationToken cancellationToken)
    {
        var reply = await spot.JoinPlayerAsync(player, request.GameId, cancellationToken);
        logger.LogInformation(
            "TicTacToeGame: actor join accepted. actor={ActorId}, gameId={GameId}, mark={Mark}",
            player.ActorId,
            request.GameId,
            reply.State.XActorId == player.ActorId ? "X" : "O");

        return reply;
    }
}
