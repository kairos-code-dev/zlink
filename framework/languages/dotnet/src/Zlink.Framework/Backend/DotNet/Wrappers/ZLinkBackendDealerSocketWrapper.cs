namespace Zlink.Framework.Backend.DotNet.Wrappers;


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

    public ValueTask DisposeAsync() => nativeSocket.DisposeAsync();
}
