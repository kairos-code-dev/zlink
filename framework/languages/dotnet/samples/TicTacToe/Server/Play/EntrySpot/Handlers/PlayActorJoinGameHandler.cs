using Systems.Zlink;
using TicTacToe.Server.Play.Actors;
using TicTacToe.Server.Play.GameSpots;

namespace TicTacToe.Server.Play.EntrySpot.Handlers;

internal sealed class PlayActorJoinGameHandler(ILogger<PlayActorJoinGameHandler> logger)
{
    [ZLinkSpotActorRequest]
    public async ValueTask<JoinGameRes> HandleAsync(
        PlayEntrySpot entrySpot,
        PlayActor actor,
        JoinGameReq message,
        CancellationToken cancellationToken)
    {
        _ = entrySpot;
        logger.LogInformation(
            "actor: JoinGameReq received. actor={ActorId}, gameId={GameId}",
            actor.ActorId,
            message.GameId);

        var spotRid = RoutingId.FromString(message.GameId);
        var joined = await actor.Context.JoinSpot(
                spotRid,
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
