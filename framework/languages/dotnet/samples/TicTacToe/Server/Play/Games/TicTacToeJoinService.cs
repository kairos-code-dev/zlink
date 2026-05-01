using Zlink;
using TicTacToe.Server.Play.Actors;

namespace TicTacToe.Server.Play.Games;

internal sealed class TicTacToeJoinService
{
    public async ValueTask<JoinGameRes> JoinAsync(
        PlayActor actor,
        JoinGameReq request,
        CancellationToken cancellationToken)
    {
        var spotRid = RoutingId.FromString(request.GameId);
        var reply = await actor.Context.JoinSpot(
                spotRid,
                new TicTacToeGameJoinReq(request.GameId, actor.ActorId))
            .WithTimeout(SampleTimeouts.Request)
            .Async<TicTacToeGameJoinRes>(cancellationToken);

        return new JoinGameRes(reply.State);
    }
}
