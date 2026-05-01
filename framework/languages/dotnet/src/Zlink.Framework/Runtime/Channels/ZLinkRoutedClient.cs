namespace Zlink.Framework.Runtime.Channels;

internal sealed class ZLinkRoutedClient(ZLinkFrameworkRuntime runtime) : IZLinkRoutedClient
{
    public IZLinkRoutedSendCall SendTo<TMessage>(
        string routerChannelId,
        RoutingId targetNodeRid,
        TMessage message)
    {
        return new ZLinkRoutedSendCall<TMessage>(runtime, routerChannelId, targetNodeRid, message);
    }

    public IZLinkRoutedRequestCall RequestTo<TRequest>(
        string routerChannelId,
        RoutingId targetNodeRid,
        TRequest request)
    {
        return new ZLinkRoutedRequestCall<TRequest>(runtime, routerChannelId, targetNodeRid, request);
    }
}

internal sealed class ZLinkRoutedSendCall<TMessage>(
    ZLinkFrameworkRuntime runtime,
    string routerChannelId,
    RoutingId targetNodeRid,
    TMessage message) : IZLinkRoutedSendCall
{
    private string? _packetName = ZLinkMessageNameResolver.ResolveFromMessage(message);

    public IZLinkRoutedSendCall WithPacketName(string packetName)
    {
        _packetName = packetName;
        return this;
    }

    public ValueTask Async(CancellationToken cancellationToken = default)
    {
        return runtime.GetRoutedChannel(routerChannelId).SubmitSendAsync(
            targetNodeRid,
            _packetName ?? throw new InvalidOperationException("Packet name is required."),
            message,
            cancellationToken);
    }
}

internal sealed class ZLinkRoutedRequestCall<TRequest>(
    ZLinkFrameworkRuntime runtime,
    string routerChannelId,
    RoutingId targetNodeRid,
    TRequest request) : IZLinkRoutedRequestCall
{
    private string? _packetName = ZLinkMessageNameResolver.ResolveFromMessage(request);
    private TimeSpan? _timeout;

    public IZLinkRoutedRequestCall WithPacketName(string packetName)
    {
        _packetName = packetName;
        return this;
    }

    public IZLinkRoutedRequestCall WithTimeout(TimeSpan timeout)
    {
        _timeout = timeout;
        return this;
    }

    public ValueTask<TReply> Async<TReply>(CancellationToken cancellationToken = default)
    {
        var timeout = _timeout ?? runtime.Registration.DefaultTimeout;
        return runtime.GetRoutedChannel(routerChannelId).RequestAsync<TRequest, TReply>(
            targetNodeRid,
            _packetName ?? throw new InvalidOperationException("Packet name is required."),
            request,
            timeout,
            cancellationToken);
    }
}
