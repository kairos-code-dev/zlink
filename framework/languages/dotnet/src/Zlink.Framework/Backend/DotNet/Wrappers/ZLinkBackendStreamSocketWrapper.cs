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

    public void OnFramedPacket(Action<string, Message, Message> handler)
    {
        nativeSocket.OnPacket((routingId, header, body) =>
        {
            handler("hex:" + routingId.ToHex(), header, body);
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

    public void BindActor(
        IZLinkBackendSpotNode node,
        RoutingId sessionRid,
        ZLinkBackendActorRef actor,
        TimeSpan timeout)
    {
        nativeSocket.BindActor(
            node.RequireNative<SpotNode>(),
            sessionRid,
            actor.ToNative(),
            timeout);
    }

    public void UnbindActor(
        IZLinkBackendSpotNode node,
        RoutingId sessionRid,
        string actorId,
        TimeSpan timeout)
    {
        nativeSocket.UnbindActor(
            node.RequireNative<SpotNode>(),
            sessionRid,
            actorId,
            timeout);
    }

    public bool SendBoundActor(
        IZLinkBackendSpotNode node,
        RoutingId sessionRid,
        string actorId,
        IReadOnlyList<Message> parts,
        SendFlags flags)
    {
        return nativeSocket.SendBoundActor(
            node.RequireNative<SpotNode>(),
            sessionRid,
            actorId,
            parts,
            flags);
    }

    public ValueTask DisposeAsync() => nativeSocket.DisposeAsync();
}
