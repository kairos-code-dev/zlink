namespace Zlink.Framework.Backend.DotNet.Wrappers;


internal sealed class ZLinkBackendRouterSocketWrapper(RouterSocket nativeSocket) : IZLinkBackendRouterSocket
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

    public Received? Recv(RecvFlags flags = RecvFlags.None)
    {
        return nativeSocket.Recv(flags);
    }

    public void Reply(
        RoutingId routingId,
        ulong requestSeq,
        Message message)
    {
        nativeSocket.Reply(routingId, requestSeq, message);
    }

    public ValueTask DisposeAsync() => nativeSocket.DisposeAsync();
}
