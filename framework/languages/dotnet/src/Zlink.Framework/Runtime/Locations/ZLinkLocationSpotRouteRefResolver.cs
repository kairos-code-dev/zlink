namespace Zlink.Framework.Runtime.Locations;

/// <summary>
/// Default <see cref="IZLinkSpotRouteRefResolver"/> over the location
/// store, replacing the removed registry-backed resolver. Registered only
/// when location stores are enabled and no custom resolver was added.
/// </summary>
internal sealed class ZLinkLocationSpotRouteRefResolver(
    ZLinkSpotMeshLocationResolver resolver) : IZLinkSpotRouteRefResolver
{
    public async ValueTask<ZLinkSpotRouteRef> ResolveSpotRouteRefAsync(
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

        return new ZLinkSpotRouteRef(
            row.MeshName,
            row.NodeRid,
            row.SpotRid,
            row.SpotKind);
    }
}
