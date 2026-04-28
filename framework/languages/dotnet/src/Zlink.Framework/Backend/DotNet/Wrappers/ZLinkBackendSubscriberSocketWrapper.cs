namespace Zlink.Framework.Backend.DotNet.Wrappers;


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

    public global::Zlink.TopicMessage? Subscribe()
    {
        return nativeSocket.Subscribe();
    }

    public ValueTask DisposeAsync() => nativeSocket.DisposeAsync();
}
