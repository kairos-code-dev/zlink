namespace Zlink.Framework.Backend.DotNet.Wrappers;


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

    public bool Send(global::Zlink.Message message, global::Zlink.SendFlags flags)
    {
        return nativeSocket.Send(message, flags);
    }

    public async ValueTask<IReadOnlyList<global::Zlink.Message>> RequestAsync(
        global::Zlink.Message message,
        TimeSpan timeout,
        CancellationToken cancellationToken)
    {
        return await nativeSocket.RequestAsync(message, timeout, cancellationToken).ConfigureAwait(false);
    }

    public ValueTask DisposeAsync() => nativeSocket.DisposeAsync();
}
