namespace Zlink.Framework.Runtime.Backend.DotNet.Adapters;


internal sealed class ZLinkDotNetChannelBackendAdapter : IZLinkChannelBackendAdapter
{
    public IZLinkBackendContext CreateContext()
    {
        return new ZLinkBackendContextWrapper(
            global::Systems.Zlink.Zlink.CreateContext());
    }

    public IZLinkBackendDiscovery CreateDiscovery(
        IZLinkBackendContext context,
        ZLinkAutoConnectType autoConnectType,
        string channelName)
    {
        var nativeContext = context.RequireNative<IContext>();

        return new ZLinkBackendDiscoveryWrapper(
            nativeContext.CreateDiscovery((AutoConnectType)autoConnectType,
                channelName));
    }

    public IZLinkBackendDealerSocket CreateDealerSocket(IZLinkBackendContext context)
    {
        var socket = context.RequireNative<IContext>().CreateDealerSocket();
        socket.Options.Linger = TimeSpan.Zero;
        return new ZLinkBackendDealerSocketWrapper(
            socket);
    }

    public IZLinkBackendRouterSocket CreateRouterSocket(IZLinkBackendContext context)
    {
        var socket = context.RequireNative<IContext>().CreateRouterSocket();
        socket.Options.Linger = TimeSpan.Zero;
        return new ZLinkBackendRouterSocketWrapper(
            socket);
    }

    public IZLinkBackendPublisherSocket CreatePublisherSocket(IZLinkBackendContext context)
    {
        var socket = context.RequireNative<IContext>().CreatePubSocket();
        socket.Options.Linger = TimeSpan.Zero;
        return new ZLinkBackendPublisherSocketWrapper(
            socket);
    }

    public IZLinkBackendSubscriberSocket CreateSubscriberSocket(IZLinkBackendContext context)
    {
        var socket = context.RequireNative<IContext>().CreateSubSocket();
        socket.Options.Linger = TimeSpan.Zero;
        return new ZLinkBackendSubscriberSocketWrapper(
            socket);
    }
}

internal sealed class ZLinkDotNetSpotBackendAdapter : IZLinkSpotBackendAdapter
{
    public IZLinkBackendSpotNode CreateSpotNode(
        IZLinkBackendContext context,
        SpotNodeMode mode)
    {
        return new ZLinkBackendSpotNodeWrapper(
            context.RequireNative<IContext>().CreateSpotNode(mode));
    }
}

internal sealed class ZLinkDotNetStreamBackendAdapter : IZLinkStreamBackendAdapter
{
    public IZLinkBackendStreamSocket CreateStreamSocket(IZLinkBackendContext context)
    {
        var socket = context.RequireNative<IContext>().CreateStreamSocket();
        return new ZLinkBackendStreamSocketWrapper(
            socket);
    }
}

internal sealed class ZLinkDotNetRegistryBackendAdapter : IZLinkRegistryBackendAdapter
{
    public IZLinkBackendRegistry CreateRegistry(IZLinkBackendContext context)
    {
        return new ZLinkBackendRegistryWrapper(
            context.RequireNative<IContext>().CreateRegistry());
    }

    public IZLinkBackendRegistryQueryClient CreateRegistryQueryClient(IZLinkBackendContext context)
    {
        return new ZLinkBackendRegistryQueryClientWrapper(
            context.RequireNative<IContext>().CreateRegistryQueryClient());
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
