using Microsoft.Extensions.DependencyInjection;
using Zlink.Framework.Backend.Contracts;

namespace Zlink.Framework.Runtime.Spots;

internal sealed class ZLinkSpotPublisherBundle : IAsyncDisposable
{
    public ZLinkSpotPublisherBundle(IZLinkBackendSpot spot)
    {
        Spot = spot;
    }

    public IZLinkBackendSpot Spot { get; }

    public async ValueTask DisposeAsync()
    {
        await Spot.DisposeAsync();
    }
}

internal sealed class ZLinkSpotAttachedChannelBundle : IAsyncDisposable
{
    private readonly HashSet<string> _manualConnections = new(StringComparer.Ordinal);

    public ZLinkSpotAttachedChannelBundle(IZLinkBackendDealerSocket socket)
    {
        Socket = socket;
    }

    public IZLinkBackendDealerSocket Socket { get; }

    public IZLinkBackendDiscovery? Discovery { get; set; }

    public bool TryAddManualConnection(string endpoint)
    {
        lock (_manualConnections)
        {
            return _manualConnections.Add(endpoint);
        }
    }

    public void RemoveManualConnection(string endpoint)
    {
        lock (_manualConnections)
        {
            _manualConnections.Remove(endpoint);
        }
    }

    public IReadOnlyList<string> ListManualConnections()
    {
        lock (_manualConnections)
        {
            return _manualConnections.OrderBy(static endpoint => endpoint, StringComparer.Ordinal).ToArray();
        }
    }

    public async ValueTask DisposeAsync()
    {
        if (Discovery is not null)
        {
            await Discovery.DisposeAsync();
        }

        await Socket.DisposeAsync();
    }
}
