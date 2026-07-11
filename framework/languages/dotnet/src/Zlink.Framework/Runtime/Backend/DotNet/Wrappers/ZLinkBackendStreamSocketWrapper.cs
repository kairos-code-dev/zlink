namespace Zlink.Framework.Runtime.Backend.DotNet.Wrappers;

internal sealed class ZLinkBackendStreamSocketWrapper(IStreamSocket nativeSocket) : IZLinkBackendStreamSocket
{
    internal IStreamSocket NativeSocket => nativeSocket;

    public void Bind(string endpoint)
    {
        nativeSocket.Bind(endpoint);
    }

    public void SetChannelName(string channelName)
    {
        nativeSocket.SetChannelName(channelName);
    }

    public void SetTlsServer(string certPath, string keyPath, bool requireClientCert)
    {
        nativeSocket.SetTlsServer(certPath, keyPath, requireClientCert);
    }

    public void OnFramedPacket(Action<string, Message, Message> handler)
    {
        nativeSocket.OnPacket((routingId, header, body) => { handler("hex:" + routingId.ToHex(), header, body); });
    }

    public bool Send(
        RoutingId routingId,
        Message payload,
        SendFlags flags)
    {
        return nativeSocket.Send(routingId)
            .Message(payload)
            .Flags(flags)
            .Submit();
    }

    public bool Send(
        RoutingId routingId,
        IReadOnlyList<Message> parts,
        SendFlags flags)
    {
        return nativeSocket.Send(routingId)
            .Messages(parts)
            .Flags(flags)
            .Submit();
    }

    public void DisconnectPeer(RoutingId routingId)
    {
        nativeSocket.DisconnectRid(routingId);
    }

    public async ValueTask BindActorAsync(
        RoutingId sessionRid,
        ZLinkBackendActorRef actor,
        TimeSpan timeout,
        CancellationToken cancellationToken)
    {
        await nativeSocket.BindActor(sessionRid, actor.ToNative())
            .Timeout(timeout)
            .Async(cancellationToken)
            .ConfigureAwait(false);
    }

    public async ValueTask UnbindActorAsync(
        RoutingId sessionRid,
        string actorId,
        TimeSpan timeout,
        CancellationToken cancellationToken)
    {
        await nativeSocket.UnbindActor(sessionRid, actorId)
            .Timeout(timeout)
            .Async(cancellationToken)
            .ConfigureAwait(false);
    }

    public bool SendBoundActor(
        RoutingId sessionRid,
        string actorId,
        IReadOnlyList<Message> parts,
        SendFlags flags)
    {
        return nativeSocket.SendBoundActor(sessionRid, actorId)
            .Messages(parts)
            .Flags(flags)
            .Submit();
    }

    public ValueTask DisposeAsync()
    {
        return nativeSocket.DisposeAsync();
    }
}
