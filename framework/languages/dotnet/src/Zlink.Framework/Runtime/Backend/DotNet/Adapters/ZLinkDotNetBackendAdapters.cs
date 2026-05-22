using ZlinkRegistry = Systems.Zlink.Registry;

namespace Zlink.Framework.Runtime.Backend.DotNet.Adapters;


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
        var socket = new DealerSocket(context.RequireNative<Context>());
        socket.Options.Linger = TimeSpan.Zero;
        return new ZLinkBackendDealerSocketWrapper(
            socket);
    }

    public IZLinkBackendRouterSocket CreateRouterSocket(IZLinkBackendContext context)
    {
        var socket = new RouterSocket(context.RequireNative<Context>());
        socket.Options.Linger = TimeSpan.Zero;
        return new ZLinkBackendRouterSocketWrapper(
            socket);
    }

    public IZLinkBackendPublisherSocket CreatePublisherSocket(IZLinkBackendContext context)
    {
        var socket = new PubSocket(context.RequireNative<Context>());
        socket.Options.Linger = TimeSpan.Zero;
        return new ZLinkBackendPublisherSocketWrapper(
            socket);
    }

    public IZLinkBackendSubscriberSocket CreateSubscriberSocket(IZLinkBackendContext context)
    {
        var socket = new SubSocket(context.RequireNative<Context>());
        socket.Options.Linger = TimeSpan.Zero;
        return new ZLinkBackendSubscriberSocketWrapper(
            socket);
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
        var socket = new StreamSocket(context.RequireNative<Context>());
        return new ZLinkBackendStreamSocketWrapper(
            socket);
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
