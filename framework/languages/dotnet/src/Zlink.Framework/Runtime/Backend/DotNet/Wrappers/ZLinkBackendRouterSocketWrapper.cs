namespace Zlink.Framework.Runtime.Backend.DotNet.Wrappers;

internal sealed class ZLinkBackendRouterSocketWrapper(IRouterSocket nativeSocket) : IZLinkBackendRouterSocket
{
    public void ApplySocketConfig(IZLinkSocketConfig config) =>
        ZLinkBackendSocketOptionsMapper.Apply(nativeSocket.Options, config);
    private readonly object _gate = new();

    internal IRouterSocket NativeSocket => nativeSocket;

    public void Bind(string endpoint)
    {
        nativeSocket.Bind(endpoint);
    }

    public void SetChannelName(string channelName)
    {
        nativeSocket.SetChannelName(channelName);
    }

    public void SetMaxMessageSize(long value)
    {
        nativeSocket.Options.MaxMessageSize = value;
    }

    public void SetSendHighWaterMark(int value)
    {
        nativeSocket.Options.SendHighWaterMark = value;
    }

    public void SetReceiveHighWaterMark(int value)
    {
        nativeSocket.Options.ReceiveHighWaterMark = value;
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

    public void SetConnectRoutingId(RoutingId routingId)
    {
        nativeSocket.Options.SetConnectRoutingId(routingId);
    }

    public void SetProbe(bool enabled)
    {
        nativeSocket.Options.Probe = enabled;
    }

    public void SetMandatory(bool mandatory)
    {
        nativeSocket.Options.Mandatory = mandatory;
    }

    public void SetHandover(bool enabled)
    {
        nativeSocket.Options.Handover = enabled;
    }

    public void SetPeerWeight(int weight)
    {
        nativeSocket.Options.PeerWeight = weight;
    }

    public int GetPeerWeight()
    {
        return nativeSocket.Options.PeerWeight;
    }

    public Received? Recv(RecvFlags flags = RecvFlags.None)
    {
        var result = Received.Create();
        lock (_gate)
        {
            if (nativeSocket.Recv(result, flags))
                return result;
        }

        result.Dispose();
        return null;
    }

    public bool Send(
        RoutingId routingId,
        Message message,
        SendFlags flags)
    {
        lock (_gate)
        {
            return nativeSocket.Send(routingId)
                .Message(message)
                .Flags(flags)
                .Submit();
        }
    }

    public bool Send(
        RoutingId routingId,
        IReadOnlyList<Message> parts,
        SendFlags flags)
    {
        lock (_gate)
        {
            return nativeSocket.Send(routingId)
                .Messages(parts)
                .Flags(flags)
                .Submit();
        }
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
        if (timeout is { } value) operation = operation.Timeout(value);

        lock (_gate)
        {
            return operation.Submit(callback);
        }
    }

    public bool Request(
        RoutingId routingId,
        IReadOnlyList<Message> parts,
        RequestCallback callback,
        SendFlags flags,
        TimeSpan? timeout)
    {
        var operation = nativeSocket.Request(routingId).Messages(parts);

        if (timeout is { } value) operation = operation.Timeout(value);

        lock (_gate)
        {
            return operation.Flags(flags).Submit(callback);
        }
    }

    // 10.0.0 removed spot addressing from the raw ROUTER socket; spot sends/requests
    // now flow through the MeshNode spot plane (ISpot.SendToSpot/RequestToSpot).
    // These stubs keep the seam compiling; see S8 route/spot-plane follow-up.
    public bool SendToSpot(
        RoutingId targetNodeRid,
        RoutingId targetSpotRid,
        IReadOnlyList<Message> parts,
        SendFlags flags)
    {
        throw new NotSupportedException(
            "ROUTER-socket spot addressing was removed in RouteMesh 10.0.0; " +
            "use the MeshNode spot plane.");
    }

    public bool RequestToSpot(
        RoutingId targetNodeRid,
        RoutingId targetSpotRid,
        IReadOnlyList<Message> parts,
        RequestCallback callback,
        SendFlags flags,
        TimeSpan? timeout)
    {
        throw new NotSupportedException(
            "ROUTER-socket spot addressing was removed in RouteMesh 10.0.0; " +
            "use the MeshNode spot plane.");
    }

    public void Reply(
        RoutingId routingId,
        ulong requestSeq,
        Message message)
    {
        lock (_gate)
        {
            nativeSocket.Reply(routingId, requestSeq)
                .Message(message)
                .Submit();
        }
    }

    public void Reply(
        RoutingId routingId,
        ulong requestSeq,
        IReadOnlyList<Message> parts)
    {
        lock (_gate)
        {
            nativeSocket.Reply(routingId, requestSeq)
                .Messages(parts)
                .Submit();
        }
    }

    public ValueTask DisposeAsync()
    {
        lock (_gate)
        {
            return nativeSocket.DisposeAsync();
        }
    }
}
