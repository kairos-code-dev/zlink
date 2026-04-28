namespace Zlink.Framework.Backend.DotNet.Wrappers;


internal sealed class ZLinkBackendStreamSocketWrapper(global::Zlink.StreamSocket nativeSocket) : IZLinkBackendStreamSocket
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

    public void OnRawPacket(Func<string, global::Zlink.Message, int> handler)
    {
        nativeSocket.OnPacket(handler.Invoke);
    }

    public void OnFramedPacket(Action<string, global::Zlink.Message, global::Zlink.Message> handler)
    {
        nativeSocket.OnFramedPacket((routingIdText, header, body) =>
        {
            handler(routingIdText, header, body);
        });
    }

    public bool Send(
        global::Zlink.RoutingId routingId,
        global::Zlink.Message payload,
        global::Zlink.SendFlags flags)
    {
        return nativeSocket.Send(routingId, payload, flags);
    }

    public bool Send(
        global::Zlink.RoutingId routingId,
        IReadOnlyList<global::Zlink.Message> parts,
        global::Zlink.SendFlags flags)
    {
        return nativeSocket.Send(routingId, parts, flags);
    }

    public ValueTask DisposeAsync() => nativeSocket.DisposeAsync();
}
