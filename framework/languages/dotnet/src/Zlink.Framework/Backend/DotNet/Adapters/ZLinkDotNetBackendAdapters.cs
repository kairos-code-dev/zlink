namespace Zlink.Framework.Backend.DotNet.Adapters;


internal sealed class ZLinkDotNetChannelBackendAdapter : IZLinkChannelBackendAdapter
{
    public IZLinkBackendContext CreateContext()
    {
        return new ZLinkBackendContextWrapper(new global::Zlink.Context());
    }

    public IZLinkBackendDiscovery CreateDiscovery(
        IZLinkBackendContext context,
        ZLinkBackendServiceType serviceType,
        string serviceName)
    {
        var nativeContext = context.RequireNative<global::Zlink.Context>();
        var nativeServiceType = serviceType switch
        {
            ZLinkBackendServiceType.Socket => global::Zlink.ServiceType.Socket,
            ZLinkBackendServiceType.Spot => global::Zlink.ServiceType.Spot,
            _ => throw new ArgumentOutOfRangeException(nameof(serviceType)),
        };

        return new ZLinkBackendDiscoveryWrapper(
            new global::Zlink.Discovery(nativeContext, nativeServiceType, serviceName));
    }

    public IZLinkBackendDealerSocket CreateDealerSocket(IZLinkBackendContext context)
    {
        return new ZLinkBackendDealerSocketWrapper(
            new global::Zlink.DealerSocket(context.RequireNative<global::Zlink.Context>()));
    }

    public IZLinkBackendRouterSocket CreateRouterSocket(IZLinkBackendContext context)
    {
        return new ZLinkBackendRouterSocketWrapper(
            new global::Zlink.RouterSocket(context.RequireNative<global::Zlink.Context>()));
    }

    public IZLinkBackendPublisherSocket CreatePublisherSocket(IZLinkBackendContext context)
    {
        return new ZLinkBackendPublisherSocketWrapper(
            new global::Zlink.PubSocket(context.RequireNative<global::Zlink.Context>()));
    }

    public IZLinkBackendSubscriberSocket CreateSubscriberSocket(IZLinkBackendContext context)
    {
        return new ZLinkBackendSubscriberSocketWrapper(
            new global::Zlink.SubSocket(context.RequireNative<global::Zlink.Context>()));
    }
}

internal sealed class ZLinkDotNetSpotBackendAdapter : IZLinkSpotBackendAdapter
{
    public IZLinkBackendSpotNode CreateSpotNode(IZLinkBackendContext context)
    {
        return new ZLinkBackendSpotNodeWrapper(
            new global::Zlink.SpotNode(context.RequireNative<global::Zlink.Context>()));
    }
}

internal sealed class ZLinkDotNetStreamBackendAdapter : IZLinkStreamBackendAdapter
{
    public IZLinkBackendStreamSocket CreateStreamSocket(IZLinkBackendContext context)
    {
        return new ZLinkBackendStreamSocketWrapper(
            new global::Zlink.StreamSocket(context.RequireNative<global::Zlink.Context>()));
    }
}

internal sealed class ZLinkDotNetRegistryBackendAdapter : IZLinkRegistryBackendAdapter
{
    public IZLinkBackendRegistry CreateRegistry(IZLinkBackendContext context)
    {
        return new ZLinkBackendRegistryWrapper(
            new global::Zlink.Registry(context.RequireNative<global::Zlink.Context>()));
    }

    public IZLinkBackendRegistryQueryClient CreateRegistryQueryClient(IZLinkBackendContext context)
    {
        return new ZLinkBackendRegistryQueryClientWrapper(
            new global::Zlink.RegistryQueryClient(context.RequireNative<global::Zlink.Context>()));
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
