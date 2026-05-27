using Systems.Zlink;
using TicTacToe.SessionGateway.Server.Play.EntrySpot;
using TicTacToe.SessionGateway.Server.Play.GameSpots;
using TicTacToe.SessionGateway.Shared.Actors;
using TicTacToe.SessionGateway.Shared.Configuration;
using TicTacToe.SessionGateway.Shared.Contracts;
using Zlink.Framework.Contracts.Handlers;

namespace TicTacToe.SessionGateway.Play.EntrySpot.Handlers;

internal sealed class JoinMatchHandler(GameNotificationPublisher notifications)
{
    [ZLinkSpotActorRequest]
    public async ValueTask<JoinMatchRes> HandleAsync(
        TicTacToeEntrySpot entrySpot,
        PlayerActor actor,
        JoinMatchReq request,
        CancellationToken cancellationToken)
    {
        _ = entrySpot;
        var result = await actor.Context
            .JoinSpot(RoutingId.FromHex(request.MatchId), request)
            .Timeout(SampleTimings.RequestTimeout)
            .SubmitAsync<JoinMatchSpotResult>(cancellationToken)
            ;
        var joined = result.Reply;
        await notifications.PublishAsync(joined.Events, cancellationToken)
            ;
        var reply = new JoinMatchRes(
            joined.MatchId,
            result.Actor.ActorId,
            joined.Mark.ToContract(),
            joined.Snapshot.ToContract());
        return reply;
    }
}
