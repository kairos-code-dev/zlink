namespace TicTacToe.Server.Play.Actors.Handlers;

internal sealed class PlayActorJoinGameHandler(
    TicTacToeJoinService games,
    ILogger<PlayActorJoinGameHandler> logger)
    : IZLinkActorPacketHandler<PlayActor, JoinGameReq>
{
    public async ValueTask HandleAsync(
        PlayActor actor,
        JoinGameReq message,
        CancellationToken cancellationToken)
    {
        logger.LogInformation(
            "actor: JoinGameReq received. actor={ActorId}, gameId={GameId}, player={PlayerId}",
            actor.ActorId,
            message.GameId,
            actor.PlayerId);

        var reply = await games.JoinAsync(actor, message, cancellationToken);
        await actor.Context.Reply(reply)
            .Async(cancellationToken);
        logger.LogInformation(
            "actor -> client: JoinGameRes sent. actor={ActorId}, gameId={GameId}, mark={Mark}",
            actor.ActorId,
            reply.State.GameId,
            reply.State.XPlayerId == actor.PlayerId ? "X" : "O");
    }
}

internal sealed class PlayActorPlaceMarkHandler(ILogger<PlayActorPlaceMarkHandler> logger)
    : IZLinkActorPacketHandler<PlayActor, PlaceMarkReq>
{
    public async ValueTask HandleAsync(
        PlayActor actor,
        PlaceMarkReq message,
        CancellationToken cancellationToken)
    {
        var gameId = actor.RequireJoinedGame();
        var game = actor.Context.GetSpot<TicTacToeGame>();

        logger.LogInformation(
            "actor: PlaceMarkReq received. actor={ActorId}, gameId={GameId}, cell={Cell}",
            actor.ActorId,
            gameId,
            message.Cell);

        var reply = await game.PlaceMarkAsync(actor, message.Cell, cancellationToken);
        await actor.Context.Reply(reply)
            .Async(cancellationToken);

        logger.LogInformation(
            "actor -> client: PlaceMarkRes sent. actor={ActorId}, gameId={GameId}, board={Board}, status={Status}",
            actor.ActorId,
            reply.State.GameId,
            reply.State.Board,
            reply.State.Status);
    }
}
