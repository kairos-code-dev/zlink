namespace Zlink.Framework.Runtime.Channels;

internal interface IZLinkMultipartRouteClient : IZLinkRouteClient
{
    ValueTask SendPartsTo(
        string routerChannelId,
        RoutingId targetNodeRid,
        string packetName,
        IReadOnlyList<Message> payloadParts,
        CancellationToken cancellationToken);

    ValueTask<TReply> RequestPartsTo<TReply>(
        string routerChannelId,
        RoutingId targetNodeRid,
        string packetName,
        IReadOnlyList<Message> payloadParts,
        TimeSpan timeout,
        CancellationToken cancellationToken);
}

internal sealed class ZLinkRouteClient(ZLinkFrameworkRuntime runtime) : IZLinkMultipartRouteClient
{
    public IZLinkSendCall Send<TMessage>(
        string routerChannelId,
        RoutingId targetNodeRid,
        TMessage message)
    {
        return new ZLinkRouteSendCall<TMessage>(runtime, routerChannelId, targetNodeRid, message);
    }

    public IZLinkRequestCall Request<TRequest>(
        string routerChannelId,
        RoutingId targetNodeRid,
        TRequest request)
    {
        return new ZLinkRouteRequestCall<TRequest>(runtime, routerChannelId, targetNodeRid, request);
    }

    public ValueTask SendPartsTo(
        string routerChannelId,
        RoutingId targetNodeRid,
        string packetName,
        IReadOnlyList<Message> payloadParts,
        CancellationToken cancellationToken)
    {
        var header = ZLinkClientCallCodec.CreateEnvelope(
            ZLinkMessageKind.Command,
            routerChannelId,
            packetName);
        return runtime.GetRouteChannel(routerChannelId)
            .SubmitSendPartsAsync(targetNodeRid, header, payloadParts, cancellationToken);
    }

    public ValueTask<TReply> RequestPartsTo<TReply>(
        string routerChannelId,
        RoutingId targetNodeRid,
        string packetName,
        IReadOnlyList<Message> payloadParts,
        TimeSpan timeout,
        CancellationToken cancellationToken)
    {
        var header = ZLinkClientCallCodec.CreateEnvelope(
            ZLinkMessageKind.Request,
            routerChannelId,
            packetName,
            timeout);
        return runtime.GetRouteChannel(routerChannelId)
            .RequestPartsAsync<TReply>(
                targetNodeRid,
                header,
                payloadParts,
                timeout,
                cancellationToken);
    }
}

internal sealed class ZLinkRouteSendCall<TMessage>(
    ZLinkFrameworkRuntime runtime,
    string routerChannelId,
    RoutingId targetNodeRid,
    TMessage message) : IZLinkSendCall
{
    private string? _packetName = ZLinkMessageNameResolver.ResolveFromMessage(message);

    public IZLinkSendCall PacketName(string packetName)
    {
        _packetName = packetName;
        return this;
    }

    public ValueTask Async(CancellationToken cancellationToken = default)
    {
        return runtime.SubmitRouteSendAsync(
            routerChannelId,
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
    TRequest request) : IZLinkRequestCall
{
    private string? _packetName = ZLinkMessageNameResolver.ResolveFromMessage(request);
    private TimeSpan? _timeout;

    public IZLinkRequestCall PacketName(string packetName)
    {
        _packetName = packetName;
        return this;
    }

    public IZLinkRequestCall Timeout(TimeSpan timeout)
    {
        _timeout = timeout;
        return this;
    }

    public ValueTask<TReply> Async<TReply>(CancellationToken cancellationToken = default)
    {
        var timeout = _timeout ?? runtime.Registration.ResolveRouteRequestTimeout(routerChannelId);
        return runtime.SubmitRouteRequestAsync<TRequest, TReply>(
            routerChannelId,
            targetNodeRid,
            _packetName ?? throw new InvalidOperationException("Packet name is required."),
            request,
            timeout,
            cancellationToken);
    }
}
