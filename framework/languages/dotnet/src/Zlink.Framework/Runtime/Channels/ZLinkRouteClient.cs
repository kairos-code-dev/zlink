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
        using (var operation = runtime.EnterOperation())
            runtime.GetRouteChannel(routerChannelId);
        ZLinkUnawaitedSubmit.Observe(
            runtime.SubmitRouteSendAsync(
                routerChannelId,
                targetNodeRid,
                ZLinkMessageNameResolver.ResolveFromMessage(message),
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
    private TimeSpan? _timeout;

    public IZLinkRequestCall Timeout(TimeSpan timeout)
    {
        ZLinkRequestTimeoutValidation.Validate(timeout, nameof(timeout));
        _timeout = timeout;
        return this;
    }

    public ValueTask<TReply> Async<TReply>(CancellationToken cancellationToken = default)
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
}

internal sealed class ZLinkRouteSpotSendCall<TMessage>(
    ZLinkFrameworkRuntime runtime,
    ZLinkResolvedSpotHandle target,
    TMessage message) : IZLinkSendCall
{
    public void Submit(CancellationToken cancellationToken = default)
    {
        ZLinkUnawaitedSubmit.Observe(SubmitAsync(cancellationToken), "route spot send submit");
    }

    private async ValueTask SubmitAsync(CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        await SubmitToSnapshotAsync(target.Snapshot, cancellationToken).ConfigureAwait(false);
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
    private TimeSpan? _timeout;

    public IZLinkRequestCall Timeout(TimeSpan timeout)
    {
        ZLinkRequestTimeoutValidation.Validate(timeout, nameof(timeout));
        _timeout = timeout;
        return this;
    }

    public async ValueTask<TReply> Async<TReply>(CancellationToken cancellationToken = default)
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
}
