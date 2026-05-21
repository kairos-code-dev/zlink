namespace Zlink.Framework.Contracts.Spots;

public interface IZLinkSpotRouteResolver
{
    ValueTask<ZLinkSpotRoute> ResolveSpotRouteAsync(
        string spotName,
        CancellationToken cancellationToken);

    ValueTask<ZLinkSpotRoute> ResolveSpotRouteAsync(
        RoutingId spotRid,
        CancellationToken cancellationToken);
}

public enum ZLinkSpotKind
{
    Invalid = 0,
    Entry = 1,
    User = 2,
}

public readonly record struct ZLinkSpotRoute(
    string RouterChannelId,
    RoutingId TargetNodeRid,
    RoutingId SpotRid,
    ZLinkSpotKind SpotKind);
