using Microsoft.Extensions.DependencyInjection;
using Zlink.Framework.Backend.Contracts;

namespace Zlink.Framework.Runtime.Spots;

internal sealed class ZLinkSpotPublisherBundle : IAsyncDisposable
{
    public ZLinkSpotPublisherBundle(
        IZLinkBackendSpot spot,
        ZLinkAsyncSubmitter? submitter = null)
    {
        Spot = spot;
        Submitter = submitter;
    }

    public IZLinkBackendSpot Spot { get; }

    public ZLinkAsyncSubmitter? Submitter { get; }

    public async ValueTask DisposeAsync()
    {
        if (Submitter is not null)
        {
            await Submitter.DisposeAsync();
        }

        await Spot.DisposeAsync();
    }
}

internal sealed class ZLinkSpotAttachedChannelBundle : IAsyncDisposable
{
    private readonly ZLinkSortedConnectionSet _manualConnections = new();

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
            return _manualConnections.Snapshot();
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
