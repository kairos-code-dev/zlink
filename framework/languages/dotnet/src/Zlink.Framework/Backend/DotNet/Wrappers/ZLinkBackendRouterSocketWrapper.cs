namespace Zlink.Framework.Backend.DotNet.Wrappers;


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

    public global::Zlink.Received? Recv()
    {
        return nativeSocket.Recv();
    }

    public void Reply(
        global::Zlink.RoutingId routingId,
        ulong requestSeq,
        global::Zlink.Message message)
    {
        nativeSocket.Reply(routingId, requestSeq, message);
    }

    public ValueTask DisposeAsync() => nativeSocket.DisposeAsync();
}
