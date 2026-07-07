namespace Zlink.Framework.Contracts.Spots;

public interface IZLinkSpotRouteRefResolver
{
    ValueTask<ZLinkSpotRouteRef> ResolveSpotRouteRefAsync(
        RoutingId spotRid,
        CancellationToken cancellationToken);
}

public enum ZLinkSpotKind
{
    Invalid = 0,
    Entry = 1,
    User = 2
}

public readonly record struct ZLinkSpotRouteRef(
    string RouterChannelId,
    RoutingId TargetNodeRid,
    RoutingId SpotRid,
    ZLinkSpotKind SpotKind);