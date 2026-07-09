namespace Zlink.Framework.Runtime.Spots;

internal sealed class ZLinkSpotManagerService(ZLinkFrameworkRuntime runtime) : IZLinkSpotManager
{
    public ValueTask<ZLinkSpotCreateResult> CreateAsync<TSpot>(
        CancellationToken cancellationToken = default)
        where TSpot : IZLinkSpot
    {
        return runtime.CreateSpotAsync<TSpot>(ZLinkMessage.Empty, cancellationToken);
    }

    public ValueTask<ZLinkSpotCreateResult> CreateAsync<TSpot>(
        ZLinkMessage request,
        CancellationToken cancellationToken = default)
        where TSpot : IZLinkSpot
    {
        return runtime.CreateSpotAsync<TSpot>(request, cancellationToken);
    }

    public ValueTask<ZLinkSpotCreateResult> GetOrCreateAsync<TSpot>(
        RoutingId spotRid,
        ZLinkMessage request,
        CancellationToken cancellationToken = default)
        where TSpot : IZLinkSpot
    {
        return runtime.GetOrCreateSpotAsync<TSpot>(spotRid, request, cancellationToken);
    }

    public ValueTask<ZLinkSpotCreateResult> GetOrCreateAsync<TSpot>(
        RoutingId spotRid,
        CancellationToken cancellationToken = default)
        where TSpot : IZLinkSpot
    {
        return runtime.GetOrCreateSpotAsync<TSpot>(spotRid, ZLinkMessage.Empty, cancellationToken);
    }

    public ValueTask<ZLinkSpotInfo?> FindAsync(RoutingId spotRid,
        CancellationToken cancellationToken = default)
    {
        return runtime.GetSpotAsync(spotRid, cancellationToken);
    }

    public ValueTask<IReadOnlyList<ZLinkSpotInfo>> ListAsync(
        CancellationToken cancellationToken = default)
    {
        return runtime.ListSpotsAsync(cancellationToken);
    }

    public ValueTask<bool> CloseAsync(
        RoutingId spotRid,
        CancellationToken cancellationToken = default)
    {
        return runtime.CloseSpotAsync(spotRid, cancellationToken);
    }
}
