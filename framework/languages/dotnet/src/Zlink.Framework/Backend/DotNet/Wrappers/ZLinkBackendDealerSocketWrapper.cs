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

    public void OnSendReady(Action handler)
    {
        nativeSocket.OnSendReady(handler);
    }

    public bool Send(Message message, SendFlags flags)
    {
        return nativeSocket.Send(message, flags);
    }

    public bool Request(
        Message message,
        Action<RequestResult, IReadOnlyList<Message>> callback,
        SendFlags flags,
        TimeSpan? timeout)
    {
        return nativeSocket.Request(message, callback, flags, timeout);
    }

    public ValueTask DisposeAsync() => nativeSocket.DisposeAsync();
}
