namespace Zlink.Framework.Runtime.Channels;

internal sealed class ZLinkRouteClient(ZLinkFrameworkRuntime runtime) : IZLinkRouteClient
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
        SpotHandle target,
        TMessage message)
    {
        return new ZLinkRouteSpotSendCall<TMessage>(runtime, RequireResolvedHandle(target), message);
    }

    public IZLinkRequestCall RequestToSpot<TRequest>(
        SpotHandle target,
        TRequest request)
    {
        return new ZLinkRouteSpotRequestCall<TRequest>(runtime, RequireResolvedHandle(target), request);
    }

    private static ZLinkResolvedSpotHandle RequireResolvedHandle(SpotHandle target)
    {
        return target as ZLinkResolvedSpotHandle
               ?? throw new ArgumentException("Spot handle was not created by this framework runtime.", nameof(target));
    }

}

internal sealed class ZLinkRouteSendCall<TMessage>(
    ZLinkFrameworkRuntime runtime,
    string routerChannelId,
    RoutingId targetNodeRid,
    TMessage message) : IZLinkSendCall
{
    public void Submit(CancellationToken cancellationToken = default)
    {
        var accepted = runtime.SubmitRouteSendAsync(
                routerChannelId,
                targetNodeRid,
                ZLinkMessageNameResolver.ResolveFromMessage(message),
                message,
                cancellationToken);
        ZLinkUnawaitedSubmit.Observe(
            accepted,
            "route send submit",
            runtime.ErrorSink,
            "RouteMeshChannel",
            "Send");
    }
}

internal sealed class ZLinkRouteRequestCall<TRequest>(
    ZLinkFrameworkRuntime runtime,
    string routerChannelId,
    RoutingId targetNodeRid,
    TRequest request) : IZLinkRequestCall
{
    private readonly ZLinkSerialTurn? _turn = ZLinkSerialTurn.Current;
    private TimeSpan? _timeout;

    public IZLinkRequestCall Timeout(TimeSpan timeout)
    {
        ZLinkRequestTimeoutValidation.Validate(timeout, nameof(timeout));
        _timeout = timeout;
        return this;
    }

    public ValueTask<TReply> Async<TReply>(CancellationToken cancellationToken = default)
    {
        return ExecuteAsync<TReply>(cancellationToken);
    }

    public void Submit<TReply>(CancellationToken cancellationToken = default)
    {
        ZLinkUnawaitedSubmit.Observe(
            ObserveAsync<TReply>(cancellationToken),
            "route request submit",
            runtime.ErrorSink);
    }

    public ValueTask<TReply> Yield<TReply>(CancellationToken cancellationToken = default)
    {
        return _turn is null
            ? ExecuteAsync<TReply>(cancellationToken)
            : _turn.YieldFrameworkCallAsync(ExecuteAsync<TReply>, cancellationToken);
    }

    private ValueTask<TReply> ExecuteAsync<TReply>(CancellationToken cancellationToken)
    {
        var timeout = _timeout ?? runtime.Registration.ResolveRouteRequestTimeout(routerChannelId);
        return runtime.SubmitRouteRequestAsync<TRequest, TReply>(
            routerChannelId,
            targetNodeRid,
            ZLinkMessageNameResolver.ResolveFromMessage(request),
            request,
            timeout,
            cancellationToken);
    }

    private async ValueTask ObserveAsync<TReply>(CancellationToken cancellationToken)
    {
        _ = await ExecuteAsync<TReply>(cancellationToken).ConfigureAwait(false);
    }
}

internal sealed class ZLinkRouteSpotSendCall<TMessage>(
    ZLinkFrameworkRuntime runtime,
    ZLinkResolvedSpotHandle target,
    TMessage message) : IZLinkSendCall
{
    public void Submit(CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var accepted = SubmitToSnapshotAsync(target.Snapshot, cancellationToken);
        ZLinkUnawaitedSubmit.Observe(
            accepted,
            "route spot send submit",
            runtime.ErrorSink,
            "RouteMeshChannel",
            "Send");
    }

    private ValueTask SubmitToSnapshotAsync(
        ZLinkSpotHandleSnapshot snapshot,
        CancellationToken cancellationToken)
    {
        var header = ZLinkClientCallCodec.CreateEnvelope(
            ZLinkMessageKind.Command,
            snapshot.RouterChannelId,
            ZLinkMessageNameResolver.ResolveFromMessage(message));
        var parts = ZLinkClientCallCodec.EncodeEnvelopeParts(header, message, runtime.Registration.Codecs);
        return runtime.SendToSpotViaRouterChannelAsync(
            snapshot.RouterChannelId,
            snapshot.NodeRid,
            snapshot.SpotRid,
            parts,
            cancellationToken);
    }
}

internal sealed class ZLinkRouteSpotRequestCall<TRequest>(
    ZLinkFrameworkRuntime runtime,
    ZLinkResolvedSpotHandle target,
    TRequest request) : IZLinkRequestCall
{
    private readonly ZLinkSerialTurn? _turn = ZLinkSerialTurn.Current;
    private TimeSpan? _timeout;

    public IZLinkRequestCall Timeout(TimeSpan timeout)
    {
        ZLinkRequestTimeoutValidation.Validate(timeout, nameof(timeout));
        _timeout = timeout;
        return this;
    }

    public ValueTask<TReply> Async<TReply>(CancellationToken cancellationToken = default)
    {
        return ExecuteAsync<TReply>(cancellationToken);
    }

    public void Submit<TReply>(CancellationToken cancellationToken = default)
    {
        ZLinkUnawaitedSubmit.Observe(
            ObserveAsync<TReply>(cancellationToken),
            "route Spot request submit",
            runtime.ErrorSink);
    }

    public ValueTask<TReply> Yield<TReply>(CancellationToken cancellationToken = default)
    {
        return _turn is null
            ? ExecuteAsync<TReply>(cancellationToken)
            : _turn.YieldFrameworkCallAsync(ExecuteAsync<TReply>, cancellationToken);
    }

    private async ValueTask<TReply> ExecuteAsync<TReply>(CancellationToken cancellationToken)
    {
        var packetName = ZLinkMessageNameResolver.ResolveFromMessage(request);
        var reply = await ZLinkSpotHandleRequestExecution.ExecuteAsync(
                target,
                snapshot =>
                {
                    var timeout = _timeout ??
                        runtime.Registration.ResolveRouteRequestTimeout(snapshot.RouterChannelId);
                    var header = ZLinkClientCallCodec.CreateEnvelope(
                        ZLinkMessageKind.Request,
                        snapshot.RouterChannelId,
                        packetName,
                        timeout);
                    var parts = ZLinkClientCallCodec.EncodeEnvelopeParts(
                        header,
                        request,
                        runtime.Registration.Codecs);
                    return runtime.RequestToSpotViaRouterChannelAsync(
                        snapshot.RouterChannelId,
                        snapshot.NodeRid,
                        snapshot.SpotRid,
                        parts,
                        timeout,
                        cancellationToken);
                },
                cancellationToken)
            .ConfigureAwait(false);
        return ZLinkClientCallCodec.DecodeEnvelopeReplyAndDispose<TReply>(
            reply,
            "SPOT route request reply is empty.",
            $"SPOT route request failed for '{packetName}'.",
            runtime.Registration.Codecs);
    }

    private async ValueTask ObserveAsync<TReply>(CancellationToken cancellationToken)
    {
        _ = await ExecuteAsync<TReply>(cancellationToken).ConfigureAwait(false);
    }
}
