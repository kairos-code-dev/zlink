namespace Zlink.Framework.Runtime.Backend.DotNet.Wrappers;


internal sealed class ZLinkBackendDealerSocketWrapper(DealerSocket nativeSocket) : IZLinkBackendDealerSocket
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

    public void Connect(string endpoint)
    {
        nativeSocket.Connect(endpoint);
    }

    public void Disconnect(string endpoint)
    {
        nativeSocket.Disconnect(endpoint);
    }

    public void AttachDiscovery(IZLinkBackendDiscovery discovery)
    {
        nativeSocket.AttachDiscovery(discovery.RequireNative<Discovery>());
    }

    public void OnSendReady(Action handler)
    {
        nativeSocket.OnSendReady(handler);
    }

    public bool Send(Message message, SendFlags flags)
    {
        return nativeSocket.Send()
            .Message(message)
            .Flags(flags)
            .Submit();
    }

    public bool Send(IReadOnlyList<Message> parts, SendFlags flags)
    {
        return nativeSocket.Send()
            .Messages(parts)
            .Flags(flags)
            .Submit();
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
        if (timeout is { } value)
        {
            operation = operation.Timeout(value);
        }

        return operation.Submit(callback);
    }

    public bool Request(
        IReadOnlyList<Message> parts,
        RequestCallback callback,
        SendFlags flags,
        TimeSpan? timeout)
    {
        var operation = nativeSocket.Request().Messages(parts);

        if (timeout is { } value)
        {
            operation = operation.Timeout(value);
        }

        return operation.Flags(flags).Submit(callback);
    }

    public Received? Recv(RecvFlags flags = RecvFlags.None)
    {
        var received = new Received();
        if (nativeSocket.Recv(received, flags))
        {
            return received;
        }

        received.Dispose();
        return null;
    }

    public bool RequestFrame(
        ulong requestSeq,
        IReadOnlyList<Message> parts,
        SendFlags flags)
    {
        return nativeSocket.RequestFrame(requestSeq, parts, flags);
    }

    public ZLinkBackendDealerReceived? RecvDealer(
        RecvFlags flags = RecvFlags.None)
    {
        var received = nativeSocket.RecvDealer(flags);
        if (received is null)
        {
            return null;
        }

        Message[] parts = new Message[received.Parts.Count];
        for (var i = 0; i < received.Parts.Count; i++)
        {
            parts[i] = received.Parts[i].Move();
        }

        var requestSeq = received.RequestSeq;
        Action<IReadOnlyList<Message>>? reply = received.MessageType
            == DealerMessageType.Request
                ? replyParts => nativeSocket.Reply(requestSeq, replyParts)
                : null;
        var result = new ZLinkBackendDealerReceived(
            received.MessageType,
            requestSeq,
            parts,
            reply);
        received.Dispose();
        return result;
    }

    public void Reply(ulong requestToken, IReadOnlyList<Message> parts)
    {
        nativeSocket.Reply(requestToken, parts);
    }

    public ValueTask DisposeAsync() => nativeSocket.DisposeAsync();
}
