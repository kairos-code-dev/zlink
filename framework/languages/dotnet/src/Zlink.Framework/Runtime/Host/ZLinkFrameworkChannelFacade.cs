namespace Zlink.Framework.Runtime.Host;

internal sealed class ZLinkFrameworkChannelFacade(
    ZLinkChannelRuntimeManager channels,
    Func<ZLinkFrameworkRuntimeState> getState)
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

    public IZLinkBackendSocket GetMonitoringSocket(string sourceName)
    {
        return channels.GetMonitoringSocket(getState(), sourceName);
    }
}
