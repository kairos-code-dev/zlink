namespace Zlink.Framework.Runtime.Core;

internal sealed class ZLinkFrameworkSpotFacade(
    ZLinkSpotRuntimeManager spots,
    Func<ZLinkFrameworkRuntimeState> getState,
    Func<CancellationToken, ValueTask<ZLinkFrameworkRuntimeState>> getStartedState)
{
    public ZLinkSpotPublisherBundle GetPublisherBundle(string channelName)
    {
        return spots.GetPublisherBundle(getState(), channelName);
    }

    public ValueTask<ZLinkSpotCreateResult> CreateAsync(
        string spotName,
        RoutingId? spotRid,
        CancellationToken cancellationToken)
    {
        return spots.CreateAsync(getState(), spotName, spotRid, cancellationToken);
    }

    public ValueTask<ZLinkSpotInfo?> GetAsync(
        RoutingId spotRid,
        CancellationToken cancellationToken)
    {
        return spots.GetAsync(getState(), spotRid, cancellationToken);
    }

    public ValueTask<IReadOnlyList<ZLinkSpotInfo>> ListAsync(CancellationToken cancellationToken)
    {
        return spots.ListAsync(getState(), cancellationToken);
    }

    public ValueTask<bool> RemoveAsync(
        RoutingId spotRid,
        CancellationToken cancellationToken)
    {
        return spots.RemoveAsync(getState(), spotRid, cancellationToken);
    }

    public async ValueTask<IZLinkEndpointConnections> GetRouterConnectionsAsync(
        string spotNodeName,
        CancellationToken cancellationToken)
    {
        return spots.GetRouterConnections(await getStartedState(cancellationToken), spotNodeName);
    }

    public async ValueTask<IZLinkEndpointConnections> GetPubSubConnectionsAsync(
        string spotNodeName,
        CancellationToken cancellationToken)
    {
        return spots.GetPubSubConnections(await getStartedState(cancellationToken), spotNodeName);
    }

    public async ValueTask<IZLinkEndpointConnections> GetChannelClientConnectionsAsync(
        string spotNodeName,
        string channelName,
        CancellationToken cancellationToken)
    {
        return spots.GetChannelClientConnections(await getStartedState(cancellationToken), spotNodeName, channelName);
    }

    public async ValueTask<IZLinkEndpointConnections> GetPublisherConnectionsAsync(
        string spotNodeName,
        string channelName,
        CancellationToken cancellationToken)
    {
        return spots.GetPublisherConnections(await getStartedState(cancellationToken), spotNodeName, channelName);
    }

    public ZLinkSpotMonitoringSnapshot GetMonitoringSnapshot(string spotNodeName)
    {
        return spots.GetMonitoringSnapshot(getState(), spotNodeName);
    }
}
