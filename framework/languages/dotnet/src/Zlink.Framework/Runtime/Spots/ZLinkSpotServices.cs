using Microsoft.Extensions.DependencyInjection;
using Zlink.Framework.Backend;

namespace Zlink.Framework.Runtime.Spots;

internal sealed class ZLinkSpotManagerService(ZLinkFrameworkRuntime runtime) : IZLinkSpotManager
{
    public ValueTask<ZLinkSpotCreateResult> CreateAsync(
        string spotName,
        CancellationToken cancellationToken = default)
    {
        return runtime.CreateSpotAsync(spotName, null, cancellationToken);
    }

    public ValueTask<ZLinkSpotCreateResult> CreateAsync(
        string spotName,
        global::Zlink.RoutingId spotRid,
        CancellationToken cancellationToken = default)
    {
        return runtime.CreateSpotAsync(spotName, spotRid, cancellationToken);
    }

    public ValueTask<ZLinkSpotInfo?> GetAsync(
        global::Zlink.RoutingId spotRid,
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
        global::Zlink.RoutingId spotRid,
        CancellationToken cancellationToken = default)
    {
        return runtime.RemoveSpotAsync(spotRid, cancellationToken);
    }
}

internal sealed class ZLinkSpotConnectionManagerService(ZLinkFrameworkRuntime runtime)
    : IZLinkSpotConnectionManager
{
    public ValueTask<IZLinkEndpointConnections> GetRouterAsync(
        string spotNodeName,
        CancellationToken cancellationToken = default)
    {
        return runtime.GetSpotRouterConnectionsAsync(spotNodeName, cancellationToken);
    }

    public ValueTask<IZLinkEndpointConnections> GetPubSubAsync(
        string spotNodeName,
        CancellationToken cancellationToken = default)
    {
        return runtime.GetSpotPubSubConnectionsAsync(spotNodeName, cancellationToken);
    }

    public ValueTask<IZLinkEndpointConnections> GetChannelClientAsync(
        string spotNodeName,
        string channelName,
        CancellationToken cancellationToken = default)
    {
        return runtime.GetSpotChannelClientConnectionsAsync(
            spotNodeName,
            channelName,
            cancellationToken);
    }

    public ValueTask<IZLinkEndpointConnections> GetSpotPublisherClientAsync(
        string spotNodeName,
        string channelName,
        CancellationToken cancellationToken = default)
    {
        return runtime.GetSpotPublisherConnectionsAsync(
            spotNodeName,
            channelName,
            cancellationToken);
    }
}
