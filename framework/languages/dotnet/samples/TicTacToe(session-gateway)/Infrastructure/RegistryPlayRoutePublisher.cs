using TicTacToe.SessionActorDispatch.Configuration;
using Zlink.Framework.Streams;

namespace TicTacToe.SessionActorDispatch.Infrastructure;

internal sealed class RegistryPlayRoutePublisher(
    RegistryPlayRouteStore routes,
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
        return routes.BindActorPlayAsync(
            actorId,
            CreateLocalPlayRoute(),
            cancellationToken);
    }

    public ValueTask BindMatchAsync(
        string matchId,
        CancellationToken cancellationToken)
    {
        return routes.BindMatchAsync(
            matchId,
            CreateLocalPlayRoute(),
            cancellationToken);
    }

    private ZLinkActorRoute CreateLocalPlayRoute()
    {
        return new ZLinkActorRoute(SampleNames.RouterChannel, topology.PlayRid);
    }
}
