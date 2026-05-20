using Systems.Zlink;
using Zlink.Framework.Contracts.Actors;

namespace Bingo.Server.Infrastructure;

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
