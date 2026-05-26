using Microsoft.Extensions.DependencyInjection;
using Zlink.Framework.Runtime.Backend.Contracts;

namespace Zlink.Framework.Runtime.Spots;

internal sealed class ZLinkSpotManagerService(ZLinkFrameworkRuntime runtime) : IZLinkSpotManager
{
    public ValueTask<ZLinkSpotCreateResult> CreateAsync<TSpot>(
        CancellationToken cancellationToken = default)
        where TSpot : IZLinkSpot
    {
        return runtime.CreateSpotAsync<TSpot>([], cancellationToken);
    }

    public ValueTask<ZLinkSpotCreateResult> CreateAsync<TSpot>(
        IReadOnlyList<Message> createParts,
        CancellationToken cancellationToken = default)
        where TSpot : IZLinkSpot
    {
        return runtime.CreateSpotAsync<TSpot>(createParts, cancellationToken);
    }

    public ValueTask<ZLinkSpotCreateResult> GetOrCreateAsync<TSpot>(
        RoutingId spotRid,
        IReadOnlyList<Message> createParts,
        CancellationToken cancellationToken = default)
        where TSpot : IZLinkSpot
    {
        return runtime.GetOrCreateSpotAsync<TSpot>(spotRid, createParts, cancellationToken);
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

    public ValueTask<bool> RemoveAsync(
        RoutingId spotRid,
        CancellationToken cancellationToken = default)
    {
        return runtime.RemoveSpotAsync(spotRid, cancellationToken);
    }
}
