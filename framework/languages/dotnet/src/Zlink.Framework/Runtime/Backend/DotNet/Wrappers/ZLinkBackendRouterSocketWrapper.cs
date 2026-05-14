namespace Zlink.Framework.Runtime.Backend.DotNet.Wrappers;


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

    public void Connect(string endpoint)
    {
        nativeSocket.Connect(endpoint);
    }

    public void Disconnect(string endpoint)
    {
        nativeSocket.Disconnect(endpoint);
    }

    public void OnSendReady(Action handler)
    {
        nativeSocket.OnSendReady(handler);
    }

    public void SetRoutingId(RoutingId routingId)
    {
        nativeSocket.SetRoutingId(routingId);
    }

    public Received? Recv(RecvFlags flags = RecvFlags.None)
    {
        // Bridge to the canonical caller-provided-storage recv. The
        // framework's IZLinkBackendRouterSocket.Recv signature predates
        // the binding migration and still allocates a fresh Received per
        // call; future work can lift the caller-provided pattern up to
        // the framework adapter too.
        var result = new Received();
        if (nativeSocket.Recv(result, flags))
            return result;
        result.Dispose();
        return null;
    }

    public bool Send(
        RoutingId routingId,
        Message message,
        SendFlags flags)
    {
        return nativeSocket.Send(routingId)
            .Message(message)
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

    public bool Request(
        RoutingId routingId,
        Message message,
        RequestCallback callback,
        SendFlags flags,
        TimeSpan? timeout)
    {
        var operation = nativeSocket.Request(routingId)
            .Message(message)
            .Flags(flags);
        if (timeout is { } value)
        {
            operation = operation.Timeout(value);
        }

        return operation.Submit(callback);
    }

    public bool Request(
        RoutingId routingId,
        IReadOnlyList<Message> parts,
        RequestCallback callback,
        SendFlags flags,
        TimeSpan? timeout)
    {
        var operation = nativeSocket.Request(routingId).Messages(parts);

        if (timeout is { } value)
        {
            operation = operation.Timeout(value);
        }

        return operation.Flags(flags).Submit(callback);
    }

    public void Reply(
        RoutingId routingId,
        ulong requestSeq,
        Message message)
    {
        nativeSocket.Reply(routingId, requestSeq)
            .Message(message)
            .Submit();
    }

    public void Reply(
        RoutingId routingId,
        ulong requestSeq,
        IReadOnlyList<Message> parts)
    {
        nativeSocket.Reply(routingId, requestSeq)
            .Messages(parts)
            .Submit();
    }

    public ValueTask DisposeAsync() => nativeSocket.DisposeAsync();
}
