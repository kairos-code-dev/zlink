namespace Zlink.Framework.Backend.DotNet.Wrappers;


internal sealed class ZLinkBackendSubscriberSocketWrapper(SubSocket nativeSocket) : IZLinkBackendSubscriberSocket
{
    public object NativeInstance => nativeSocket;

    public void Bind(string endpoint)
    {
        nativeSocket.Bind(endpoint);
    }

    public void SetChannelName(string channelName)
    {
        ZLinkBackendNativeAccess.SetNativeChannelName(nativeSocket, channelName);
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

    public void SetSubscription(string topic)
    {
        nativeSocket.SetSubscription(topic);
    }

    public TopicMessage? Subscribe(RecvFlags flags = RecvFlags.None)
    {
        return nativeSocket.Subscribe(flags);
    }

    public ValueTask DisposeAsync() => nativeSocket.DisposeAsync();
}
