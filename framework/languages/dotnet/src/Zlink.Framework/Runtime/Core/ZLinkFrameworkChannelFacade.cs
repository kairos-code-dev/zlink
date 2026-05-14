namespace Zlink.Framework.Runtime.Core;

internal sealed class ZLinkFrameworkChannelFacade(
    ZLinkChannelRuntimeManager channels,
    Func<ZLinkFrameworkRuntimeState> getState,
    Func<CancellationToken, ValueTask<ZLinkFrameworkRuntimeState>> getStartedState)
{
    public ZLinkChannelRuntimeBundle GetOrCreateClientBundle(string channelName)
    {
        return channels.GetOrCreateClientBundle(getState(), channelName);
    }

    public ZLinkChannelRuntimeBundle GetOrCreatePublisherBundle(string channelName)
    {
        return channels.GetOrCreatePublisherBundle(getState(), channelName);
    }

    public ZLinkRouteChannelRuntime GetRouteChannel(string routerChannelId)
    {
        return channels.GetRouteChannel(getState(), routerChannelId);
    }

    public async ValueTask<IZLinkEndpointConnections> GetClientConnectionsAsync(
        string channelName,
        CancellationToken cancellationToken)
    {
        return channels.GetClientConnections(await getStartedState(cancellationToken), channelName);
    }

    public async ValueTask<IZLinkEndpointConnections> GetSubscriberConnectionsAsync(
        string channelName,
        CancellationToken cancellationToken)
    {
        return channels.GetSubscriberConnections(await getStartedState(cancellationToken), channelName);
    }

    public IZLinkBackendSocket GetMonitoringSocket(string sourceName)
    {
        return channels.GetMonitoringSocket(getState(), sourceName);
    }
}
