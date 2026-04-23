namespace Zlink.Framework.Backend;

internal sealed class ZLinkDotNetBackendAdapterFactory : IZLinkBackendAdapterFactory
{
    private static readonly IZLinkChannelBackendAdapter ChannelAdapter = new ZLinkDotNetChannelBackendAdapter();
    private static readonly IZLinkSpotBackendAdapter SpotAdapter = new ZLinkDotNetSpotBackendAdapter();
    private static readonly IZLinkStreamBackendAdapter StreamAdapter = new ZLinkDotNetStreamBackendAdapter();
    private static readonly IZLinkRegistryBackendAdapter RegistryAdapter = new ZLinkDotNetRegistryBackendAdapter();
    private static readonly IZLinkMonitoringBackendAdapter MonitoringAdapter = new ZLinkDotNetMonitoringBackendAdapter();

    public IZLinkChannelBackendAdapter CreateChannelAdapter() => ChannelAdapter;

    public IZLinkSpotBackendAdapter CreateSpotAdapter() => SpotAdapter;

    public IZLinkStreamBackendAdapter CreateStreamAdapter() => StreamAdapter;

    public IZLinkRegistryBackendAdapter CreateRegistryAdapter() => RegistryAdapter;

    public IZLinkMonitoringBackendAdapter CreateMonitoringAdapter() => MonitoringAdapter;
}

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

    public IZLinkBackendServiceMonitor OpenDiscoveryMonitor(IZLinkBackendDiscovery discovery)
    {
        var nativeMonitor = discovery.RequireNative<global::Zlink.Discovery>().MonitorOpen();
        return new ZLinkBackendServiceMonitorWrapper(nativeMonitor);
    }
}

internal static class ZLinkBackendNativeAccess
{
    public static T RequireNative<T>(this IZLinkBackendObject backendObject)
        where T : class
    {
        return backendObject.NativeInstance as T
            ?? throw new InvalidOperationException(
                $"Expected native instance '{typeof(T).FullName}'.");
    }

    public static global::Zlink.SocketMonitor OpenNativeMonitor(this IZLinkBackendSocket socket)
    {
        return socket.NativeInstance switch
        {
            global::Zlink.DealerSocket native => native.MonitorOpen(),
            global::Zlink.RouterSocket native => native.MonitorOpen(),
            global::Zlink.PubSocket native => native.MonitorOpen(),
            global::Zlink.SubSocket native => native.MonitorOpen(),
            global::Zlink.StreamSocket native => native.MonitorOpen(),
            _ => throw new InvalidOperationException("Expected a native socket instance."),
        };
    }
}

internal sealed class ZLinkBackendContextWrapper(global::Zlink.Context nativeContext) : IZLinkBackendContext
{
    public object NativeInstance => nativeContext;

    public ValueTask DisposeAsync() => nativeContext.DisposeAsync();
}

internal sealed class ZLinkBackendDiscoveryWrapper(global::Zlink.Discovery nativeDiscovery) : IZLinkBackendDiscovery
{
    public object NativeInstance => nativeDiscovery;

    public void ConnectRegistry(string endpoint)
    {
        nativeDiscovery.ConnectRegistry(endpoint);
    }

    public ValueTask DisposeAsync() => nativeDiscovery.DisposeAsync();
}

internal sealed class ZLinkBackendDealerSocketWrapper(global::Zlink.DealerSocket nativeSocket) : IZLinkBackendDealerSocket
{
    public object NativeInstance => nativeSocket;

    public void Bind(string endpoint)
    {
        nativeSocket.Bind(endpoint);
    }

    public void SetChannelName(string channelName)
    {
        nativeSocket.SetChannelName(channelName);
    }

    public void Connect(string endpoint)
    {
        nativeSocket.Connect(endpoint);
    }

    public void Disconnect(string endpoint)
    {
        nativeSocket.Disconnect(endpoint);
    }

    public void AttachDiscovery(IZLinkBackendDiscovery discovery)
    {
        nativeSocket.AttachDiscovery(discovery.RequireNative<global::Zlink.Discovery>());
    }

    public ValueTask DisposeAsync() => nativeSocket.DisposeAsync();
}

internal sealed class ZLinkBackendRouterSocketWrapper(global::Zlink.RouterSocket nativeSocket) : IZLinkBackendRouterSocket
{
    public object NativeInstance => nativeSocket;

    public void Bind(string endpoint)
    {
        nativeSocket.Bind(endpoint);
    }

    public void SetChannelName(string channelName)
    {
        nativeSocket.SetChannelName(channelName);
    }

    public void AttachDiscovery(IZLinkBackendDiscovery discovery)
    {
        nativeSocket.AttachDiscovery(discovery.RequireNative<global::Zlink.Discovery>());
    }

    public ValueTask DisposeAsync() => nativeSocket.DisposeAsync();
}

internal sealed class ZLinkBackendPublisherSocketWrapper(global::Zlink.PubSocket nativeSocket) : IZLinkBackendPublisherSocket
{
    public object NativeInstance => nativeSocket;

    public void Bind(string endpoint)
    {
        nativeSocket.Bind(endpoint);
    }

    public void SetChannelName(string channelName)
    {
        nativeSocket.SetChannelName(channelName);
    }

    public void AttachDiscovery(IZLinkBackendDiscovery discovery)
    {
        nativeSocket.AttachDiscovery(discovery.RequireNative<global::Zlink.Discovery>());
    }

    public ValueTask DisposeAsync() => nativeSocket.DisposeAsync();
}

internal sealed class ZLinkBackendSubscriberSocketWrapper(global::Zlink.SubSocket nativeSocket) : IZLinkBackendSubscriberSocket
{
    public object NativeInstance => nativeSocket;

    public void Bind(string endpoint)
    {
        nativeSocket.Bind(endpoint);
    }

    public void SetChannelName(string channelName)
    {
        nativeSocket.SetChannelName(channelName);
    }

    public void Connect(string endpoint)
    {
        nativeSocket.Connect(endpoint);
    }

    public void Disconnect(string endpoint)
    {
        nativeSocket.Disconnect(endpoint);
    }

    public void AttachDiscovery(IZLinkBackendDiscovery discovery)
    {
        nativeSocket.AttachDiscovery(discovery.RequireNative<global::Zlink.Discovery>());
    }

    public void SetSubscription(string topic)
    {
        nativeSocket.SetSubscription(topic);
    }

    public ValueTask DisposeAsync() => nativeSocket.DisposeAsync();
}

internal sealed class ZLinkBackendStreamSocketWrapper(global::Zlink.StreamSocket nativeSocket) : IZLinkBackendStreamSocket
{
    public object NativeInstance => nativeSocket;

    public void Bind(string endpoint)
    {
        nativeSocket.Bind(endpoint);
    }

    public void SetChannelName(string channelName)
    {
        nativeSocket.SetChannelName(channelName);
    }

    public ValueTask DisposeAsync() => nativeSocket.DisposeAsync();
}

internal sealed class ZLinkBackendRegistryWrapper(global::Zlink.Registry nativeRegistry) : IZLinkBackendRegistry
{
    public object NativeInstance => nativeRegistry;

    public void Bind(string pubEndpoint, string routerEndpoint)
    {
        nativeRegistry.Bind(pubEndpoint, routerEndpoint);
    }

    public ValueTask DisposeAsync() => nativeRegistry.DisposeAsync();
}

internal sealed class ZLinkBackendRegistryQueryClientWrapper(global::Zlink.RegistryQueryClient nativeClient)
    : IZLinkBackendRegistryQueryClient
{
    public object NativeInstance => nativeClient;

    public void Connect(string endpoint)
    {
        nativeClient.Connect(endpoint);
    }

    public ValueTask DisposeAsync() => nativeClient.DisposeAsync();
}

internal sealed class ZLinkBackendSpotNodeWrapper(global::Zlink.SpotNode nativeSpotNode) : IZLinkBackendSpotNode
{
    public object NativeInstance => nativeSpotNode;

    public void Bind(string endpoint)
    {
        nativeSpotNode.Bind(endpoint);
    }

    public void AttachDiscovery(IZLinkBackendDiscovery discovery)
    {
        nativeSpotNode.AttachDiscovery(discovery.RequireNative<global::Zlink.Discovery>());
    }

    public ValueTask DisposeAsync() => nativeSpotNode.DisposeAsync();
}

internal sealed class ZLinkBackendSocketMonitorWrapper(global::Zlink.SocketMonitor nativeMonitor) : IZLinkBackendSocketMonitor
{
    public object NativeInstance => nativeMonitor;

    public ValueTask DisposeAsync() => nativeMonitor.DisposeAsync();
}

internal sealed class ZLinkBackendServiceMonitorWrapper(global::Zlink.ServiceMonitor nativeMonitor) : IZLinkBackendServiceMonitor
{
    public object NativeInstance => nativeMonitor;

    public ValueTask DisposeAsync() => nativeMonitor.DisposeAsync();
}
