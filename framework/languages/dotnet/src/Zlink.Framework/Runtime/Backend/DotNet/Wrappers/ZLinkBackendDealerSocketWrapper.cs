namespace Zlink.Framework.Runtime.Backend.DotNet.Wrappers;

internal sealed class ZLinkBackendDealerSocketWrapper(IDealerSocket nativeSocket) : IZLinkBackendDealerSocket
{
    private readonly object _gate = new();

    // DEALER 바인딩의 PeerWeight 옵션은 set-only 이므로 마지막으로 적용한 값을 캐시한다(core 기본 100).
    // framework 가 유일한 writer 이므로 set 한 값을 read 로 돌려주는 것은 정합하다.
    private int _peerWeight = 100;

    public object NativeInstance => nativeSocket;

    public void Bind(string endpoint)
    {
        nativeSocket.Bind(endpoint);
    }

    public void SetChannelName(string channelName)
    {
        nativeSocket.SetChannelName(channelName);
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

    public void SetRoutingId(RoutingId routingId)
    {
        nativeSocket.SetRoutingId(routingId);
    }

    public void OnSendReady(Action handler)
    {
        nativeSocket.OnSendReady(handler);
    }

    public void SetPeerWeight(int weight)
    {
        // native set 은 core 의 socket public-API lock(socket_public_api_lock_scope_t) 아래 직렬화된다
        // — framework 가 caller 스레드에서 dealer.Send 를 호출하는 기존 패턴과 같은 동시성 경계다.
        nativeSocket.Options.PeerWeight = weight;
        Volatile.Write(ref _peerWeight, weight);
    }

    public int GetPeerWeight()
    {
        return Volatile.Read(ref _peerWeight);
    }

    public bool Send(Message message, SendFlags flags)
    {
        lock (_gate)
        {
            return nativeSocket.Send()
                .Message(message)
                .Flags(flags)
                .Submit();
        }
    }

    public bool Send(IReadOnlyList<Message> parts, SendFlags flags)
    {
        lock (_gate)
        {
            return nativeSocket.Send()
                .Messages(parts)
                .Flags(flags)
                .Submit();
        }
    }

    public bool Request(
        Message message,
        RequestCallback callback,
        SendFlags flags,
        TimeSpan? timeout)
    {
        var operation = nativeSocket.Request()
            .Message(message)
            .Flags(flags);
        if (timeout is { } value) operation = operation.Timeout(value);

        lock (_gate)
        {
            return operation.Submit(callback);
        }
    }

    public bool Request(
        IReadOnlyList<Message> parts,
        RequestCallback callback,
        SendFlags flags,
        TimeSpan? timeout)
    {
        var operation = nativeSocket.Request().Messages(parts);

        if (timeout is { } value) operation = operation.Timeout(value);

        lock (_gate)
        {
            return operation.Flags(flags).Submit(callback);
        }
    }

    public Received? Recv(RecvFlags flags = RecvFlags.None)
    {
        var received = Received.Create();
        lock (_gate)
        {
            if (nativeSocket.Recv(received, flags)) return received;
        }

        received.Dispose();
        return null;
    }

    public ValueTask DisposeAsync()
    {
        lock (_gate)
        {
            return nativeSocket.DisposeAsync();
        }
    }
}
