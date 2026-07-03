namespace Zlink.Framework.Runtime.Locations;

/// <summary>
/// Resolves a spot rid to its live location row by trying every spot mesh
/// channel name registered on this node (the same value the auto-connect
/// host advertises under: SpotDiscoveryChannelName ?? SpotNodeName). Every
/// try reads the store — resolvers have no cache. Scheduled for removal
/// with the address-based egress (spot-address messaging draft §10.3).
/// </summary>
internal sealed class ZLinkSpotLocationRidResolver
{
    private readonly IReadOnlyList<string> _meshNames;
    private readonly ZLinkStoreLocationResolvers _rows;

    internal ZLinkSpotLocationRidResolver(
        ZLinkFrameworkRegistration registration,
        ZLinkStoreLocationResolvers rows)
    {
        _rows = rows;
        _meshNames = registration.SpotNodes.Values
            .Select(static node => node.SpotDiscoveryChannelName ?? node.SpotNodeName)
            .Concat(registration.SpotDiscoveries.Keys)
            .Distinct(StringComparer.Ordinal)
            .ToArray();
    }

    internal async ValueTask<ZLinkSpotLocation?> ResolveAsync(
        RoutingId spotRid,
        CancellationToken cancellationToken)
    {
        foreach (var meshName in _meshNames)
        {
            var row = await _rows.ResolveSpotRowAsync(
                    new ZLinkSpotLocationKey(meshName, spotRid),
                    cancellationToken)
                .ConfigureAwait(false);
            if (row is not null)
            {
                return row;
            }
        }

        return null;
    }
}

/// <summary>
/// Default <see cref="IZLinkSpotRemoteAddressResolver"/> over the location
/// store, replacing the removed registry-backed resolver. Registered only
/// when location stores are enabled and no custom resolver was added.
/// </summary>
internal sealed class ZLinkLocationSpotRemoteAddressResolver(
    ZLinkSpotLocationRidResolver resolver) : IZLinkSpotRemoteAddressResolver
{
    public async ValueTask<ZLinkSpotRemoteAddress> ResolveSpotRemoteAddressAsync(
        RoutingId spotRid,
        CancellationToken cancellationToken)
    {
        var row = await resolver.ResolveAsync(spotRid, cancellationToken).ConfigureAwait(false);
        if (row is null)
        {
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.SpotRouteNotFound,
                $"SPOT '{spotRid}' has no live location row in any registered spot mesh.");
        }

        return new ZLinkSpotRemoteAddress(
            row.MeshName,
            row.NodeRid,
            row.SpotRid,
            row.SpotKind);
    }
}
