namespace Zlink.Framework.Runtime.Channels;

internal sealed class ZLinkRouteClient(ZLinkFrameworkRuntime runtime) : IZLinkRouteClient
{
    public IZLinkRouteSendCall SendTo<TMessage>(
        string routerChannelId,
        RoutingId targetNodeRid,
        TMessage message)
    {
        return new ZLinkRouteSendCall<TMessage>(runtime, routerChannelId, targetNodeRid, message);
    }

    public IZLinkRouteRequestCall RequestTo<TRequest>(
        string routerChannelId,
        RoutingId targetNodeRid,
        TRequest request)
    {
        return new ZLinkRouteRequestCall<TRequest>(runtime, routerChannelId, targetNodeRid, request);
    }
}

internal sealed class ZLinkRouteSendCall<TMessage>(
    ZLinkFrameworkRuntime runtime,
    string routerChannelId,
    RoutingId targetNodeRid,
    TMessage message) : IZLinkRouteSendCall
{
    private string? _packetName = ZLinkMessageNameResolver.ResolveFromMessage(message);

    public IZLinkRouteSendCall WithPacketName(string packetName)
    {
        _packetName = packetName;
        return this;
    }

    public ValueTask Submit(CancellationToken cancellationToken = default)
    {
        return runtime.GetRouteChannel(routerChannelId).SubmitSendAsync(
            targetNodeRid,
            _packetName ?? throw new InvalidOperationException("Packet name is required."),
            message,
            cancellationToken);
    }
}

internal sealed class ZLinkRouteRequestCall<TRequest>(
    ZLinkFrameworkRuntime runtime,
    string routerChannelId,
    RoutingId targetNodeRid,
    TRequest request) : IZLinkRouteRequestCall
{
    private string? _packetName = ZLinkMessageNameResolver.ResolveFromMessage(request);
    private TimeSpan? _timeout;

    public IZLinkRouteRequestCall WithPacketName(string packetName)
    {
        _packetName = packetName;
        return this;
    }

    public IZLinkRouteRequestCall WithTimeout(TimeSpan timeout)
    {
        _timeout = timeout;
        return this;
    }

    public ValueTask<TReply> SubmitAsync<TReply>(CancellationToken cancellationToken = default)
    {
        var timeout = _timeout ?? runtime.Registration.DefaultTimeout;
        return runtime.GetRouteChannel(routerChannelId).RequestAsync<TRequest, TReply>(
            targetNodeRid,
            _packetName ?? throw new InvalidOperationException("Packet name is required."),
            request,
            timeout,
            cancellationToken);
    }
}
