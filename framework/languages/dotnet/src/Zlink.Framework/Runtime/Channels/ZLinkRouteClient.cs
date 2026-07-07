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
    public IZLinkSendCall SendToNode<TMessage>(
        string routerChannelId,
        RoutingId targetNodeRid,
        TMessage message)
    {
        return new ZLinkRouteSendCall<TMessage>(runtime, routerChannelId, targetNodeRid, message);
    }

    public IZLinkRequestCall RequestToNode<TRequest>(
        string routerChannelId,
        RoutingId targetNodeRid,
        TRequest request)
    {
        return new ZLinkRouteRequestCall<TRequest>(runtime, routerChannelId, targetNodeRid, request);
    }

    public IZLinkSendCall SendToSpot<TMessage>(
        string routerChannelId,
        SpotRef address,
        TMessage message)
    {
        return new ZLinkRouteSpotSendCall<TMessage>(runtime, routerChannelId, address, message);
    }

    public IZLinkRequestCall RequestToSpot<TRequest>(
        string routerChannelId,
        SpotRef address,
        TRequest request)
    {
        return new ZLinkRouteSpotRequestCall<TRequest>(runtime, routerChannelId, address, request);
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

    public void Submit(CancellationToken cancellationToken = default)
    {
        runtime.GetRouteChannel(routerChannelId);
        ZLinkUnawaitedSubmit.Observe(
            runtime.SubmitRouteSendAsync(
                routerChannelId,
                targetNodeRid,
                _packetName ?? throw new InvalidOperationException("Packet name is required."),
                message,
                cancellationToken),
            "route send submit");
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

internal sealed class ZLinkRouteSpotSendCall<TMessage>(
    ZLinkFrameworkRuntime runtime,
    string routerChannelId,
    SpotRef address,
    TMessage message) : IZLinkSendCall
{
    private string? _packetName = ZLinkMessageNameResolver.ResolveFromMessage(message);

    public IZLinkSendCall PacketName(string packetName)
    {
        _packetName = packetName;
        return this;
    }

    public void Submit(CancellationToken cancellationToken = default)
    {
        ZLinkUnawaitedSubmit.Observe(SubmitAsync(cancellationToken), "route spot send submit");
    }

    private ValueTask SubmitAsync(CancellationToken cancellationToken)
    {
        var header = ZLinkClientCallCodec.CreateEnvelope(
            ZLinkMessageKind.Command,
            routerChannelId,
            _packetName ?? throw new InvalidOperationException("Packet name is required."));
        var parts = ZLinkClientCallCodec.EncodeEnvelopeParts(header, message, runtime.Registration.Codecs);
        return runtime.SendToSpotViaRouterChannelAsync(
            routerChannelId,
            address.NodeRid,
            address.SpotRid,
            parts,
            cancellationToken);
    }
}

internal sealed class ZLinkRouteSpotRequestCall<TRequest>(
    ZLinkFrameworkRuntime runtime,
    string routerChannelId,
    SpotRef address,
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

    public async ValueTask<TReply> Async<TReply>(CancellationToken cancellationToken = default)
    {
        var timeout = _timeout ?? runtime.Registration.ResolveRouteRequestTimeout(routerChannelId);
        var header = ZLinkClientCallCodec.CreateEnvelope(
            ZLinkMessageKind.Request,
            routerChannelId,
            _packetName ?? throw new InvalidOperationException("Packet name is required."),
            timeout);
        var parts = ZLinkClientCallCodec.EncodeEnvelopeParts(header, request, runtime.Registration.Codecs);
        var reply = await runtime.RequestToSpotViaRouterChannelAsync(
                routerChannelId,
                address.NodeRid,
                address.SpotRid,
                parts,
                timeout,
                cancellationToken)
            .ConfigureAwait(false);
        return ZLinkClientCallCodec.DecodeEnvelopeReplyAndDispose<TReply>(
            reply,
            "SPOT route request reply is empty.",
            $"SPOT route request failed for '{_packetName}'.",
            runtime.Registration.Codecs);
    }
}
