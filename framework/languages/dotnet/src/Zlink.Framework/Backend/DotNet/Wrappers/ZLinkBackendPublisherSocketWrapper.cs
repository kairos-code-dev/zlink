namespace Zlink.Framework.Backend.DotNet.Wrappers;


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

    public bool Publish(
        string topic,
        global::Zlink.Message message,
        global::Zlink.SendFlags flags)
    {
        return nativeSocket.Publish(topic, message, flags);
    }

    public ValueTask DisposeAsync() => nativeSocket.DisposeAsync();
}
