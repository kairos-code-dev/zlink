using TicTacToe.SessionActorDispatch.Configuration;
using TicTacToe.SessionGateway.Infrastructure;
using TicTacToe.SessionGateway.Infrastructure.Configuration;
using Zlink;
using Zlink.Framework.Streams;

namespace TicTacToe.SessionActorDispatch.Infrastructure;

public sealed class RegistryPlayRoutePublisher(
    RegistryPlayRouteStore actorRoutes,
    ISpotRouteWriter spotRoutes,
    SampleTopology topology)
{
    public async ValueTask BindInitialActorPlayRoutesAsync(
        IEnumerable<string> actorIds,
        CancellationToken cancellationToken)
    {
        foreach (var actorId in actorIds)
        {
            await BindActorPlayAsync(actorId, cancellationToken).ConfigureAwait(false);
        }
    }

    public ValueTask BindActorPlayAsync(
        string actorId,
        CancellationToken cancellationToken)
    {
        return actorRoutes.BindActorPlayAsync(
            actorId,
            CreateLocalPlayRoute(),
            cancellationToken);
    }

    public ValueTask BindSpotRouteAsync(
        RoutingId spotRid,
        CancellationToken cancellationToken)
    {
        return spotRoutes.BindSpotRouteAsync(
            spotRid,
            CreateLocalPlayRoute(),
            cancellationToken);
    }

    private ZLinkActorRoute CreateLocalPlayRoute()
    {
        return new ZLinkActorRoute(SampleNames.RouterChannel, topology.PlayRid);
    }
}
