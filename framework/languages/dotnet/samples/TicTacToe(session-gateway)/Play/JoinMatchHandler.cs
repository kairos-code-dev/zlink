using TicTacToe.SessionActorDispatch.Configuration;
using TicTacToe.SessionActorDispatch.Contracts;
using TicTacToe.SessionActorDispatch.Infrastructure;
using TicTacToe.SessionGateway.Play;
using Systems.Zlink;
using Zlink.Framework.Spots;

namespace TicTacToe.SessionActorDispatch.Play;

internal sealed class JoinMatchHandler(
    RegistryPlayRoutePublisher routes,
    GameNotificationPublisher notifications)
    : IZLinkEntrySpotActorRequestHandler<PlayerActor, JoinMatchReq, JoinMatchRes>
{
    public async ValueTask<JoinMatchRes> HandleAsync(
        PlayerActor actor,
        JoinMatchReq request,
        CancellationToken cancellationToken)
    {
        var result = await actor.Context
            .JoinSpot(RoutingId.FromString(request.MatchId), request)
            .WithTimeout(SampleTimings.RequestTimeout)
            .Submit<JoinMatchSpotResult>(cancellationToken)
            .ConfigureAwait(false);
        await routes.BindActorPlayAsync(actor.ActorId, cancellationToken)
            .ConfigureAwait(false);
        await notifications.PublishAsync(result.Events, cancellationToken)
            .ConfigureAwait(false);
        var reply = new JoinMatchRes(
            result.MatchId,
            result.ActorId,
            result.Mark.ToContract(),
            result.Snapshot.ToContract());
        return reply;
    }
}
