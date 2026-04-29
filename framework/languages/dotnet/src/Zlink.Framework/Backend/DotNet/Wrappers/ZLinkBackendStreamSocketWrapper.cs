namespace Zlink.Framework.Backend.DotNet.Wrappers;


internal sealed class ZLinkBackendStreamSocketWrapper(StreamSocket nativeSocket) : IZLinkBackendStreamSocket
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

    public void OnRawPacket(Func<string, Message, int> handler)
    {
        nativeSocket.OnPacket(handler.Invoke);
    }

    public void OnFramedPacket(Action<string, Message, Message> handler)
    {
        nativeSocket.OnFramedPacket((routingIdText, header, body) =>
        {
            handler(routingIdText, header, body);
        });
    }

    public bool Send(
        RoutingId routingId,
        Message payload,
        SendFlags flags)
    {
        return nativeSocket.Send(routingId, payload, flags);
    }

    public bool Send(
        RoutingId routingId,
        IReadOnlyList<Message> parts,
        SendFlags flags)
    {
        return nativeSocket.Send(routingId, parts, flags);
    }

    public void DisconnectPeer(RoutingId routingId)
    {
        nativeSocket.DisconnectRid(routingId);
    }

    public ValueTask DisposeAsync() => nativeSocket.DisposeAsync();
}
