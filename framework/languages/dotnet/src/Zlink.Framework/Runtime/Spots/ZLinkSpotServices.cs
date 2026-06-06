using Microsoft.Extensions.DependencyInjection;
using Zlink.Framework.Runtime.Backend.Contracts;

namespace Zlink.Framework.Runtime.Spots;

internal sealed class ZLinkSpotManagerService(ZLinkFrameworkRuntime runtime) : IZLinkSpotManager
{
    public async ValueTask<ZLinkSpotCreateResult> CreateAsync<TSpot>(
        CancellationToken cancellationToken = default)
        where TSpot : IZLinkSpot
    {
        using var request = Message.From(ReadOnlySpan<byte>.Empty);
        return await runtime.CreateSpotAsync<TSpot>(request, cancellationToken);
    }

    public ValueTask<ZLinkSpotCreateResult> CreateAsync<TSpot>(
        Message request,
        CancellationToken cancellationToken = default)
        where TSpot : IZLinkSpot
    {
        return runtime.CreateSpotAsync<TSpot>(request, cancellationToken);
    }

    public ValueTask<ZLinkSpotCreateResult> GetOrCreateAsync<TSpot>(
        RoutingId spotRid,
        Message request,
        CancellationToken cancellationToken = default)
        where TSpot : IZLinkSpot
    {
        return runtime.GetOrCreateSpotAsync<TSpot>(spotRid, request, cancellationToken);
    }

    public async ValueTask<ZLinkSpotCreateResult> GetOrCreateAsync<TSpot>(
        RoutingId spotRid,
        CancellationToken cancellationToken = default)
        where TSpot : IZLinkSpot
    {
        using var request = Message.From(ReadOnlySpan<byte>.Empty);
        return await runtime.GetOrCreateSpotAsync<TSpot>(spotRid, request, cancellationToken);
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
