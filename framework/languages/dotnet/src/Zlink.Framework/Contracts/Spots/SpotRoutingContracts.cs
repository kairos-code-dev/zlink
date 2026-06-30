namespace Zlink.Framework.Contracts.Spots;

public interface IZLinkSpotRemoteAddressResolver
{
    ValueTask<ZLinkSpotRemoteAddress> ResolveSpotRemoteAddressAsync(
        RoutingId spotRid,
        CancellationToken cancellationToken);
}

public enum ZLinkSpotKind
{
    Invalid = 0,
    Entry = 1,
    User = 2
}

public readonly record struct ZLinkSpotRemoteAddress(
    string RouterChannelId,
    RoutingId TargetNodeRid,
    RoutingId SpotRid,
    ZLinkSpotKind SpotKind);