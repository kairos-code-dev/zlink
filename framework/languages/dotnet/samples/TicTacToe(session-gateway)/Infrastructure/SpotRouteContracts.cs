using Systems.Zlink;
using Zlink.Framework.Streams;

namespace TicTacToe.SessionGateway.Infrastructure;

public interface ISpotRouteResolver
{
    ValueTask<ZLinkActorRoute> ResolveSpotRouteAsync(
        RoutingId spotRid,
        CancellationToken cancellationToken);
}

public interface ISpotRouteWriter
{
    ValueTask BindSpotRouteAsync(
        RoutingId spotRid,
        ZLinkActorRoute route,
        CancellationToken cancellationToken);
}
