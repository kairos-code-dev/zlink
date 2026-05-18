using Systems.Zlink;
using TicTacToe.Server.Play.Games;
using Zlink.Framework.Contracts.Spots;

namespace TicTacToe.Server.Play.Actors.Handlers;

internal sealed class PlayActorJoinGameHandler(ILogger<PlayActorJoinGameHandler> logger)
    : IZLinkEntrySpotActorRequestHandler<PlayActor, JoinGameReq, JoinGameRes>
{
    public async ValueTask<JoinGameRes> HandleAsync(
        PlayActor actor,
        JoinGameReq message,
        CancellationToken cancellationToken)
    {
        logger.LogInformation(
            "actor: JoinGameReq received. actor={ActorId}, gameId={GameId}",
            actor.ActorId,
            message.GameId);

        var spotRid = RoutingId.FromString(message.GameId);
        var joined = await actor.Context.JoinSpot(
                ZLinkSpotId.FromRoutingId(spotRid),
                new TicTacToeGameJoinReq(message.GameId, actor.ActorId))
            .Timeout(SampleTimeouts.Request)
            .SubmitAsync<TicTacToeGameJoinRes>(cancellationToken);

        var reply = new JoinGameRes(joined.State);
        logger.LogInformation(
            "actor -> client: JoinGameRes returned. actor={ActorId}, gameId={GameId}, mark={Mark}",
            actor.ActorId,
            reply.State.GameId,
            reply.State.XActorId == actor.ActorId ? "X" : "O");
        return reply;
    }
}

internal sealed class PlayActorPlaceMarkHandler(ILogger<PlayActorPlaceMarkHandler> logger)
    : IZLinkSpotActorRequestHandler<TicTacToeGame, PlayActor, PlaceMarkReq, PlaceMarkRes>
{
    public async ValueTask<PlaceMarkRes> HandleAsync(
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

        logger.LogInformation(
            "actor -> client: PlaceMarkRes returned. actor={ActorId}, gameId={GameId}, board={Board}, status={Status}",
            actor.ActorId,
            reply.State.GameId,
            reply.State.Board,
            reply.State.Status);
        return reply;
    }
}
