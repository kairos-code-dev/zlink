namespace Zlink.Framework.Runtime.Channels;

using Zlink.Framework.Runtime.Messaging;

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
    //  Node-direct metadata needs the router-seam threading tracked in gap 90
    //  §12.36; refusing beats silently dropping the caller's frame.
    public IZLinkSendCall Metadata(string key, string value)
    {
        throw ZLinkMeshCallSupport.NodeRouteMetadataPending();
    }

    public IZLinkSendCall Metadata(ZLinkMessageMetadata metadata)
    {
        throw ZLinkMeshCallSupport.NodeRouteMetadataPending();
    }

    public ZLinkSubmitResult TrySubmit()
    {
        throw ZLinkMeshCallSupport.TrySubmitPendingSyncAdmission();
    }

    public async ValueTask<ZLinkSubmitResult> SubmitAsync(
        CancellationToken cancellationToken = default)
    {
        try
        {
            await runtime.SubmitRouteSendAsync(
                    routerChannelId,
                    targetNodeRid,
                    ZLinkMessageNameResolver.ResolveFromMessage(message),
                    message,
                    cancellationToken)
                .ConfigureAwait(false);
        }
        catch (TimeoutException)
        {
            return new ZLinkSubmitResult(ZLinkSubmitStatus.TimedOut);
        }

        return new ZLinkSubmitResult(ZLinkSubmitStatus.Submitted);
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

    public IZLinkRequestCall Metadata(string key, string value)
    {
        throw ZLinkMeshCallSupport.NodeRouteMetadataPending();
    }

    public IZLinkRequestCall Metadata(ZLinkMessageMetadata metadata)
    {
        throw ZLinkMeshCallSupport.NodeRouteMetadataPending();
    }

    public ValueTask<TReply> Async<TReply>(CancellationToken cancellationToken = default)
    {
        return ExecuteAsync<TReply>(cancellationToken);
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

}

internal sealed class ZLinkRouteSpotSendCall<TMessage>(
    ZLinkFrameworkRuntime runtime,
    ZLinkResolvedSpotHandle target,
    TMessage message) : IZLinkSendCall
{
    private readonly ZLinkCallMetadata _metadata = new();

    public IZLinkSendCall Metadata(string key, string value)
    {
        _metadata.Set(key, value);
        return this;
    }

    public IZLinkSendCall Metadata(ZLinkMessageMetadata metadata)
    {
        _metadata.Merge(metadata);
        return this;
    }

    public ZLinkSubmitResult TrySubmit()
    {
        throw ZLinkMeshCallSupport.TrySubmitPendingSyncAdmission();
    }

    public async ValueTask<ZLinkSubmitResult> SubmitAsync(
        CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var snapshot = target.Snapshot;
        var header = ZLinkClientCallCodec.CreateEnvelope(
            ZLinkMessageKind.Command,
            snapshot.RouterChannelId,
            ZLinkMessageNameResolver.ResolveFromMessage(message));
        var parts = ZLinkClientCallCodec.EncodeEnvelopeParts(header, message, runtime.Registration.Codecs);
        try
        {
            await runtime.SendToSpotViaRouterChannelAsync(
                    snapshot.RouterChannelId,
                    snapshot.NodeRid,
                    snapshot.SpotRid,
                    (ulong)snapshot.Generation,
                    parts,
                    cancellationToken,
                    _metadata.Encode())
                .ConfigureAwait(false);
        }
        catch (TimeoutException)
        {
            return new ZLinkSubmitResult(ZLinkSubmitStatus.TimedOut);
        }

        return new ZLinkSubmitResult(ZLinkSubmitStatus.Submitted);
    }
}

internal sealed class ZLinkRouteSpotRequestCall<TRequest>(
    ZLinkFrameworkRuntime runtime,
    ZLinkResolvedSpotHandle target,
    TRequest request) : IZLinkRequestCall
{
    private readonly ZLinkCallMetadata _metadata = new();
    private readonly ZLinkSerialTurn? _turn = ZLinkSerialTurn.Current;
    private TimeSpan? _timeout;

    public IZLinkRequestCall Timeout(TimeSpan timeout)
    {
        ZLinkRequestTimeoutValidation.Validate(timeout, nameof(timeout));
        _timeout = timeout;
        return this;
    }

    public IZLinkRequestCall Metadata(string key, string value)
    {
        _metadata.Set(key, value);
        return this;
    }

    public IZLinkRequestCall Metadata(ZLinkMessageMetadata metadata)
    {
        _metadata.Merge(metadata);
        return this;
    }

    public ValueTask<TReply> Async<TReply>(CancellationToken cancellationToken = default)
    {
        return ExecuteAsync<TReply>(cancellationToken);
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
                        (ulong)snapshot.Generation,
                        parts,
                        timeout,
                        cancellationToken,
                        _metadata.Encode());
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
