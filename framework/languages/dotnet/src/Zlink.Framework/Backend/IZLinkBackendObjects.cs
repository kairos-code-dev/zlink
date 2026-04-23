namespace Zlink.Framework.Backend;

internal enum ZLinkBackendServiceType
{
    Socket = 1,
    Spot = 2,
}

internal interface IZLinkBackendObject
{
    object NativeInstance { get; }
}

internal interface IZLinkBackendContext : IZLinkBackendObject, IAsyncDisposable
{
}

internal interface IZLinkBackendDiscovery : IZLinkBackendObject, IAsyncDisposable
{
    void ConnectRegistry(string endpoint);
}

internal interface IZLinkBackendSocket : IZLinkBackendObject, IAsyncDisposable
{
    void Bind(string endpoint);

    void SetChannelName(string channelName);
}

internal interface IZLinkBackendConnectableSocket : IZLinkBackendSocket
{
    void Connect(string endpoint);

    void Disconnect(string endpoint);
}

internal interface IZLinkBackendDealerSocket : IZLinkBackendConnectableSocket
{
    void AttachDiscovery(IZLinkBackendDiscovery discovery);
}

internal interface IZLinkBackendRouterSocket : IZLinkBackendSocket
{
    void AttachDiscovery(IZLinkBackendDiscovery discovery);
}

internal interface IZLinkBackendPublisherSocket : IZLinkBackendSocket
{
    void AttachDiscovery(IZLinkBackendDiscovery discovery);
}

internal interface IZLinkBackendSubscriberSocket : IZLinkBackendConnectableSocket
{
    void AttachDiscovery(IZLinkBackendDiscovery discovery);

    void SetSubscription(string topic);
}

internal interface IZLinkBackendStreamSocket : IZLinkBackendSocket
{
}

internal interface IZLinkBackendRegistry : IZLinkBackendObject, IAsyncDisposable
{
    void Bind(string pubEndpoint, string routerEndpoint);
}

internal interface IZLinkBackendRegistryQueryClient : IZLinkBackendObject, IAsyncDisposable
{
    void Connect(string endpoint);
}

internal interface IZLinkBackendSpotNode : IZLinkBackendObject, IAsyncDisposable
{
    void Bind(string endpoint);

    void AttachDiscovery(IZLinkBackendDiscovery discovery);
}

internal interface IZLinkBackendSocketMonitor : IZLinkBackendObject, IAsyncDisposable
{
}

internal interface IZLinkBackendServiceMonitor : IZLinkBackendObject, IAsyncDisposable
{
}
