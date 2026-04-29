namespace Zlink.Framework.Backend.DotNet.Wrappers;


internal sealed class ZLinkBackendDealerSocketWrapper(DealerSocket nativeSocket) : IZLinkBackendDealerSocket
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
        nativeSocket.AttachDiscovery(discovery.RequireNative<Discovery>());
    }

    public bool Send(Message message, SendFlags flags)
    {
        return nativeSocket.Send(message, flags);
    }

    public async ValueTask<IReadOnlyList<Message>> RequestAsync(
        Message message,
        TimeSpan timeout,
        CancellationToken cancellationToken)
    {
        return await nativeSocket.RequestAsync(message, timeout, cancellationToken).ConfigureAwait(false);
    }

    public ValueTask DisposeAsync() => nativeSocket.DisposeAsync();
}
