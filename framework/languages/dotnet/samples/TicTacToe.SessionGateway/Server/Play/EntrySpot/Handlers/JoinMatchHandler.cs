using TicTacToe.SessionGateway.Shared.Actors;
using TicTacToe.SessionGateway.Server.Play.EntrySpot;
using TicTacToe.SessionGateway.Server.Play.GameSpots;
using TicTacToe.SessionGateway.Shared.Configuration;
using TicTacToe.SessionGateway.Shared.Contracts;
using Zlink.Framework.Contracts.Handlers;
using Systems.Zlink;

namespace TicTacToe.SessionGateway.Server.Play.EntrySpot.Handlers;

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
            .JoinSpot(RoutingId.FromString(request.MatchId), request)
            .Timeout(SampleTimings.RequestTimeout)
            .SubmitAsync<JoinMatchSpotResult>(cancellationToken)
            ;
        await notifications.PublishAsync(result.Events, cancellationToken)
            ;
        var reply = new JoinMatchRes(
            result.MatchId,
            result.ActorId,
            result.Mark.ToContract(),
            result.Snapshot.ToContract());
        return reply;
    }
}
