namespace Zlink.Framework.Backend.DotNet.Wrappers;


internal sealed class ZLinkBackendPublisherSocketWrapper(PubSocket nativeSocket) : IZLinkBackendPublisherSocket
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
        nativeSocket.AttachDiscovery(discovery.RequireNative<Discovery>());
    }

    public bool Publish(
        string topic,
        Message message,
        SendFlags flags)
    {
        return nativeSocket.Publish(topic, message, flags);
    }

    public ValueTask DisposeAsync() => nativeSocket.DisposeAsync();
}
