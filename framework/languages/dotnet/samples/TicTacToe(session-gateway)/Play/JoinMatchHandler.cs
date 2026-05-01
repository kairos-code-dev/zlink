using TicTacToe.SessionActorDispatch.Configuration;
using TicTacToe.SessionActorDispatch.Contracts;
using TicTacToe.SessionActorDispatch.Infrastructure;
using Zlink.Framework.Streams;

namespace TicTacToe.SessionActorDispatch.Play;

internal sealed class JoinMatchHandler(
    TicTacToeGameService games,
    RegistryPlayRouteStore routes,
    GameNotificationPublisher notifications,
    SampleTopology topology)
    : IZLinkActorRequestHandler<JoinMatchReq, JoinMatchRes>
{
    public async ValueTask<JoinMatchRes> HandleAsync(
        JoinMatchReq request,
        ZLinkActorRequestContext context,
        CancellationToken cancellationToken)
    {
        var result = games.Join(request.MatchId, context.ActorId);
        await routes.BindActorPlayAsync(
                context.ActorId,
                new ZLinkPlayRoute(SampleNames.RouterChannel, topology.PlayRid),
                cancellationToken)
            .ConfigureAwait(false);
        await notifications.PublishAsync(context.SessionProxy, result.Events, cancellationToken)
            .ConfigureAwait(false);
        return new JoinMatchRes(
            result.MatchId,
            result.ActorId,
            result.Mark.ToContract(),
            result.Snapshot.ToContract());
    }
}
