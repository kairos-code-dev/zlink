using ZlinkRegistry = Systems.Zlink.Registry;

namespace Zlink.Framework.Backend.DotNet.Adapters;


internal sealed class ZLinkDotNetChannelBackendAdapter : IZLinkChannelBackendAdapter
{
    public IZLinkBackendContext CreateContext()
    {
        return new ZLinkBackendContextWrapper(new Context());
    }

    public IZLinkBackendDiscovery CreateDiscovery(
        IZLinkBackendContext context,
        ZLinkAutoConnectType autoConnectType,
        string channelName)
    {
        var nativeContext = context.RequireNative<Context>();

        return new ZLinkBackendDiscoveryWrapper(
            new Discovery(nativeContext, (AutoConnectType)autoConnectType, channelName));
    }

    public IZLinkBackendDealerSocket CreateDealerSocket(IZLinkBackendContext context)
    {
        return new ZLinkBackendDealerSocketWrapper(
            new DealerSocket(context.RequireNative<Context>()));
    }

    public IZLinkBackendRouterSocket CreateRouterSocket(IZLinkBackendContext context)
    {
        return new ZLinkBackendRouterSocketWrapper(
            new RouterSocket(context.RequireNative<Context>()));
    }

    public IZLinkBackendPublisherSocket CreatePublisherSocket(IZLinkBackendContext context)
    {
        return new ZLinkBackendPublisherSocketWrapper(
            new PubSocket(context.RequireNative<Context>()));
    }

    public IZLinkBackendSubscriberSocket CreateSubscriberSocket(IZLinkBackendContext context)
    {
        return new ZLinkBackendSubscriberSocketWrapper(
            new SubSocket(context.RequireNative<Context>()));
    }
}

internal sealed class ZLinkDotNetSpotBackendAdapter : IZLinkSpotBackendAdapter
{
    public IZLinkBackendSpotNode CreateSpotNode(IZLinkBackendContext context)
    {
        return new ZLinkBackendSpotNodeWrapper(
            new SpotNode(context.RequireNative<Context>()));
    }
}

internal sealed class ZLinkDotNetStreamBackendAdapter : IZLinkStreamBackendAdapter
{
    public IZLinkBackendStreamSocket CreateStreamSocket(IZLinkBackendContext context)
    {
        return new ZLinkBackendStreamSocketWrapper(
            new StreamSocket(context.RequireNative<Context>()));
    }
}

internal sealed class ZLinkDotNetRegistryBackendAdapter : IZLinkRegistryBackendAdapter
{
    public IZLinkBackendRegistry CreateRegistry(IZLinkBackendContext context)
    {
        return new ZLinkBackendRegistryWrapper(
            new ZlinkRegistry(context.RequireNative<Context>()));
    }

    public IZLinkBackendRegistryQueryClient CreateRegistryQueryClient(IZLinkBackendContext context)
    {
        return new ZLinkBackendRegistryQueryClientWrapper(
            new RegistryQueryClient(context.RequireNative<Context>()));
    }
}

internal sealed class ZLinkDotNetMonitoringBackendAdapter : IZLinkMonitoringBackendAdapter
{
    public IZLinkBackendSocketMonitor OpenSocketMonitor(IZLinkBackendSocket socket)
    {
        var nativeMonitor = socket.OpenNativeMonitor();
        return new ZLinkBackendSocketMonitorWrapper(nativeMonitor);
    }
}
