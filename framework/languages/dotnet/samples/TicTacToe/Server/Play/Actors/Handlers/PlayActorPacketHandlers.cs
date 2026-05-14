using TicTacToe.Server.Play.Games;
using Zlink.Framework.Contracts.Spots;

namespace TicTacToe.Server.Play.Actors.Handlers;

internal sealed class PlayActorJoinGameHandler(
    TicTacToeJoinService games,
    ILogger<PlayActorJoinGameHandler> logger)
    : IZLinkEntrySpotActorSendHandler<PlayActor, JoinGameReq>
{
    public async ValueTask HandleAsync(
        PlayActor actor,
        JoinGameReq message,
        CancellationToken cancellationToken)
    {
        logger.LogInformation(
            "actor: JoinGameReq received. actor={ActorId}, gameId={GameId}",
            actor.ActorId,
            message.GameId);

        var reply = await games.JoinAsync(actor, message, cancellationToken);
        await actor.Context.Reply(reply)
            .Submit(cancellationToken);
        logger.LogInformation(
            "actor -> client: JoinGameRes sent. actor={ActorId}, gameId={GameId}, mark={Mark}",
            actor.ActorId,
            reply.State.GameId,
            reply.State.XActorId == actor.ActorId ? "X" : "O");
    }
}

internal sealed class PlayActorPlaceMarkHandler(ILogger<PlayActorPlaceMarkHandler> logger)
    : IZLinkSpotActorSendHandler<TicTacToeGame, PlayActor, PlaceMarkReq>
{
    public async ValueTask HandleAsync(
        TicTacToeGame spot,
        PlayActor actor,
        PlaceMarkReq message,
        CancellationToken cancellationToken)
    {
        var gameId = actor.RequireJoinedGame();
        logger.LogInformation(
            "actor: PlaceMarkReq received. actor={ActorId}, gameId={GameId}, cell={Cell}",
            actor.ActorId,
            gameId,
            message.Cell);

        var reply = await spot.PlaceMarkAsync(actor, message.Cell, cancellationToken);
        await actor.Context.Reply(reply)
            .Submit(cancellationToken);

        logger.LogInformation(
            "actor -> client: PlaceMarkRes sent. actor={ActorId}, gameId={GameId}, board={Board}, status={Status}",
            actor.ActorId,
            reply.State.GameId,
            reply.State.Board,
            reply.State.Status);
    }
}
