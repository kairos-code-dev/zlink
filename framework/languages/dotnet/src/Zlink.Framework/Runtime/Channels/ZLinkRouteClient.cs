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

    public ValueTask SendPartsTo(
        string routerChannelId,
        RoutingId targetNodeRid,
        string packetName,
        IReadOnlyList<Message> payloadParts,
        CancellationToken cancellationToken)
    {
        var header = CreateHeader(
            ZLinkMessageKind.Command,
            routerChannelId,
            packetName,
            deadline: null);
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
        var header = CreateHeader(
            ZLinkMessageKind.Request,
            routerChannelId,
            packetName,
            DateTimeOffset.UtcNow.Add(timeout));
        return runtime.GetRouteChannel(routerChannelId)
            .RequestPartsAsync<TReply>(
                targetNodeRid,
                header,
                payloadParts,
                timeout,
                cancellationToken);
    }

    private static ZLinkEnvelopeHeader CreateHeader(
        ZLinkMessageKind kind,
        string routerChannelId,
        string packetName,
        DateTimeOffset? deadline)
    {
        return new ZLinkEnvelopeHeader(
            kind,
            routerChannelId,
            packetName,
            ZLinkEnvelopeCodec.DefaultContentType,
            Guid.NewGuid().ToString("N"),
            deadline,
            null,
            null,
            null);
    }
}

internal sealed class ZLinkRouteSendCall<TMessage>(
    ZLinkFrameworkRuntime runtime,
    string routerChannelId,
    RoutingId targetNodeRid,
    TMessage message) : IZLinkRouteSendCall
{
    private string? _packetName = ZLinkMessageNameResolver.ResolveFromMessage(message);

    public IZLinkRouteSendCall PacketName(string packetName)
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

    public IZLinkRouteRequestCall PacketName(string packetName)
    {
        _packetName = packetName;
        return this;
    }

    public IZLinkRouteRequestCall Timeout(TimeSpan timeout)
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
