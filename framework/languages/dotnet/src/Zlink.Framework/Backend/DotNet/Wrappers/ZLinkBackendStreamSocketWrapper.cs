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
        if (parts.Count == 0)
        {
            throw new ArgumentException("At least one message part is required.", nameof(parts));
        }

        var operation = nativeSocket.Send(routingId).Message(parts[0]);
        for (var index = 1; index < parts.Count; index++)
        {
            operation = operation.Message(parts[index]);
        }

        return operation.Flags(flags).Submit();
    }

    public void DisconnectPeer(RoutingId routingId)
    {
        nativeSocket.DisconnectRid(routingId);
    }

    public void BindActor(
        RoutingId sessionRid,
        ZLinkBackendActorRef actor,
        TimeSpan timeout)
    {
        nativeSocket.BindActor(sessionRid, actor.ToNative())
            .Timeout(timeout)
            .SubmitAsync()
            .GetAwaiter()
            .GetResult();
    }

    public void UnbindActor(
        RoutingId sessionRid,
        string actorId,
        TimeSpan timeout)
    {
        nativeSocket.UnbindActor(sessionRid, actorId)
            .Timeout(timeout)
            .SubmitAsync()
            .GetAwaiter()
            .GetResult();
    }

    public bool SendBoundActor(
        RoutingId sessionRid,
        string actorId,
        IReadOnlyList<Message> parts,
        SendFlags flags)
    {
        if (parts.Count == 0)
        {
            throw new ArgumentException("At least one message part is required.", nameof(parts));
        }

        var operation = nativeSocket.SendBoundActor(sessionRid, actorId).Message(parts[0]);
        for (var index = 1; index < parts.Count; index++)
        {
            operation = operation.Message(parts[index]);
        }

        return operation.Flags(flags).Submit();
    }

    public ValueTask DisposeAsync() => nativeSocket.DisposeAsync();
}
