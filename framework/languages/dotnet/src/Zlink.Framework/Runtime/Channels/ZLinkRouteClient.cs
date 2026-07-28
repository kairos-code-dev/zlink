namespace Zlink.Framework.Runtime.Channels;

using Zlink.Framework.Runtime.Messaging;

internal sealed class ZLinkRouteClient(ZLinkFrameworkRuntime runtime) : IZLinkRouteClient
{
    public IZLinkSendCall SendToNode<TMessage>(
        string meshName,
        RoutingId targetNodeRid,
        TMessage message)
    {
        return new ZLinkRouteSendCall<TMessage>(runtime, meshName, targetNodeRid, message);
    }

    public IZLinkRequestCall RequestToNode<TRequest>(
        string meshName,
        RoutingId targetNodeRid,
        TRequest request)
    {
        return new ZLinkRouteRequestCall<TRequest>(runtime, meshName, targetNodeRid, request);
    }

    public IZLinkSendCall SendToChannel<TMessage>(
        string channelName,
        TMessage message)
    {
        return new ZLinkChannelSendCall<TMessage>(runtime, channelName, message);
    }

    public IZLinkRequestCall RequestToChannel<TRequest>(
        string channelName,
        TRequest request)
    {
        return new ZLinkChannelRequestCall<TRequest>(runtime, channelName, request);
    }

}

internal sealed class ZLinkSpotClient(ZLinkFrameworkRuntime runtime) : IZLinkSpotClient
{
    public IZLinkSpotSendCall SendToSpot<TMessage>(string spotId, TMessage message) =>
        new ZLinkInstanceSpotSendCall<TMessage>(runtime, spotId, message);

    public IZLinkSpotRequestCall RequestToSpot<TRequest>(string spotId, TRequest request) =>
        new ZLinkInstanceSpotRequestCall<TRequest>(runtime, spotId, request);

}

internal sealed class ZLinkChannelSendCall<TMessage>(
    ZLinkFrameworkRuntime runtime,
    string channelName,
    TMessage message) : IZLinkSendCall
{
    private readonly ZLinkCallMetadata _metadata = new();
    private readonly ZLinkOneWayCallGate _submission = new("Channel send");

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

    public async ValueTask Async(
        CancellationToken cancellationToken = default)
    {
        _submission.Claim();
        cancellationToken.ThrowIfCancellationRequested();
        var parts = Encode();
        var result = await runtime.SendToChannelAsync(
                channelName,
                parts,
                cancellationToken,
                _metadata.Encode())
            .ConfigureAwait(false);
        ZLinkOneWaySubmitOutcome.EnsureAccepted(result, "Channel send");
    }

    private IReadOnlyList<Message> Encode()
    {
        var header = ZLinkClientCallCodec.CreateEnvelope(
            ZLinkMessageKind.Command,
            channelName,
            ZLinkMessageNameResolver.ResolveFromMessage(message));
        return ZLinkClientCallCodec.EncodeEnvelopeParts(header, message, runtime.Registration.Codecs);
    }
}

internal sealed class ZLinkChannelRequestCall<TRequest>(
    ZLinkFrameworkRuntime runtime,
    string channelName,
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
        return ZLinkApplicationExecutionContext
            .RequireYieldTurn(_turn, "Channel request")
            .YieldFrameworkCallAsync(ExecuteAsync<TReply>, cancellationToken);
    }

    private async ValueTask<TReply> ExecuteAsync<TReply>(CancellationToken cancellationToken)
    {
        var packetName = ZLinkMessageNameResolver.ResolveFromMessage(request);
        var timeout = _timeout ?? runtime.Registration.ResolveChannelRequestTimeout(channelName);
        var header = ZLinkClientCallCodec.CreateEnvelope(
            ZLinkMessageKind.Request,
            channelName,
            packetName,
            timeout);
        var parts = ZLinkClientCallCodec.EncodeEnvelopeParts(header, request, runtime.Registration.Codecs);
        var reply = await runtime
            .RequestToChannelAsync(
                channelName,
                parts,
                timeout,
                cancellationToken,
                _metadata.Encode())
            .ConfigureAwait(false);
        return ZLinkClientCallCodec.DecodeEnvelopeReplyAndDispose<TReply>(
            reply,
            "Channel request reply is empty.",
            $"Channel request failed for '{packetName}'.",
            runtime.Registration.Codecs);
    }
}

internal sealed class ZLinkRouteSendCall<TMessage>(
    ZLinkFrameworkRuntime runtime,
    string meshName,
    RoutingId targetNodeRid,
    TMessage message) : IZLinkSendCall
{
    private readonly ZLinkCallMetadata _metadata = new();
    private readonly string _messageName = ZLinkMessageNameResolver.ResolveFromMessage(message);
    private readonly ZLinkOneWayCallGate _submission = new("MeshNode send");

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

    public async ValueTask Async(
        CancellationToken cancellationToken = default)
    {
        _submission.Claim();
        cancellationToken.ThrowIfCancellationRequested();
        var nodeRuntime = runtime.GetMeshNodeRuntime(meshName);
        var parts = Encode();
        var handedOff = false;
        try
        {
            runtime.EnsureKnownRouteMeshPeer(
                meshName,
                targetNodeRid,
                $"packet '{_messageName}'");
            handedOff = true;
            var result = await nodeRuntime
                .SendToNodeAsync(targetNodeRid, parts, cancellationToken, _metadata.Encode())
                .ConfigureAwait(false);
            ZLinkOneWaySubmitOutcome.EnsureAccepted(result, "Node send");
        }
        catch
        {
            if (!handedOff) DisposeBeforeHandoff(parts);
            throw;
        }
    }

    internal static void DisposeBeforeHandoff(IReadOnlyList<Message> parts) =>
        ZLinkMessageParts.DisposeAll(parts);

    private IReadOnlyList<Message> Encode()
    {
        var header = ZLinkClientCallCodec.CreateEnvelope(
            ZLinkMessageKind.Command,
            meshName,
            _messageName);
        return ZLinkClientCallCodec.EncodeEnvelopeParts(
            header,
            message,
            runtime.Registration.Codecs);
    }
}

internal sealed class ZLinkRouteRequestCall<TRequest>(
    ZLinkFrameworkRuntime runtime,
    string meshName,
    RoutingId targetNodeRid,
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
        throw new ZLinkFrameworkException(
            ZLinkFrameworkErrorKind.InvalidOperation,
            "MeshNode request does not support Yield.");
    }

    private async ValueTask<TReply> ExecuteAsync<TReply>(CancellationToken cancellationToken)
    {
        var nodeRuntime = runtime.GetMeshNodeRuntime(meshName);
        var timeout = _timeout ?? nodeRuntime.Registration.DefaultRequestTimeout
            ?? runtime.Registration.DefaultRequestTimeout;
        var packetName = ZLinkMessageNameResolver.ResolveFromMessage(request);
        runtime.EnsureKnownRouteMeshPeer(meshName, targetNodeRid, $"packet '{packetName}'");
        var header = ZLinkClientCallCodec.CreateEnvelope(
            ZLinkMessageKind.Request,
            meshName,
            packetName,
            timeout);
        var parts = ZLinkClientCallCodec.EncodeEnvelopeParts(
            header,
            request,
            runtime.Registration.Codecs);
        var reply = await nodeRuntime.RequestToNodeAsync(
                targetNodeRid,
                parts,
                timeout,
                cancellationToken,
                _metadata.Encode())
            .ConfigureAwait(false);
        return ZLinkClientCallCodec.DecodeEnvelopeReplyAndDispose<TReply>(
            reply,
            "Node request reply is empty.",
            $"Node request failed for '{packetName}'.",
            runtime.Registration.Codecs);
    }

}

internal sealed class ZLinkRouteSpotSendCall<TMessage>(
    ZLinkFrameworkRuntime runtime,
    ZLinkResolvedSpotHandle target,
    TMessage message) : IZLinkSendCall
{
    private readonly ZLinkCallMetadata _metadata = new();
    private readonly ZLinkOneWayCallGate _submission = new("Spot send");

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

    public async ValueTask Async(
        CancellationToken cancellationToken = default)
    {
        _submission.Claim();
        cancellationToken.ThrowIfCancellationRequested();
        var snapshot = target.Snapshot;
        var header = ZLinkClientCallCodec.CreateEnvelope(
            ZLinkMessageKind.Command,
            snapshot.RouterChannelId,
            ZLinkMessageNameResolver.ResolveFromMessage(message));
        var parts = ZLinkClientCallCodec.EncodeEnvelopeParts(header, message, runtime.Registration.Codecs);
        try
        {
            var result = await runtime.SendToSpotViaRouterChannelAsync(
                    snapshot.RouterChannelId,
                    snapshot.NodeRid,
                    snapshot.SpotId,
                    (ulong)snapshot.Generation,
                    snapshot.NodeGeneration,
                    snapshot.AuthorityOwnerGeneration,
                    snapshot.OwnerLeaseGeneration,
                    parts,
                    cancellationToken,
                    _metadata.Encode())
                .ConfigureAwait(false);
            ZLinkOneWaySubmitOutcome.EnsureAccepted(
                result,
                "Spot send",
                ZLinkFrameworkErrorKind.NotFound);
        }
        catch (ZLinkFrameworkException error)
            when (ZLinkSpotHandleRequestExecution.IsStaleRoute(error))
        {
            target.InvalidateRoute();
            throw;
        }
    }
}

internal sealed class ZLinkRouteSpotRequestCall<TRequest>(
    ZLinkFrameworkRuntime runtime,
    ZLinkResolvedSpotHandle target,
    TRequest request) : IZLinkRequestCall
{
    private readonly ZLinkCallMetadata _metadata = new();
    private readonly ZLinkApplicationExecutionScope? _executionScope =
        ZLinkApplicationExecutionContext.Current;
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
        ZLinkApplicationExecutionContext.RejectSpotRequestWhenSameGate(
            target.Snapshot.SpotId,
            _executionScope);
        return ExecuteAsync<TReply>(cancellationToken);
    }

    public ValueTask<TReply> Yield<TReply>(CancellationToken cancellationToken = default)
    {
        ZLinkApplicationExecutionContext.RejectSpotRequestWhenSameGate(
            target.Snapshot.SpotId,
            _executionScope);
        return ZLinkApplicationExecutionContext
            .RequireYieldTurn(_turn, "Spot request")
            .YieldFrameworkCallAsync(ExecuteAsync<TReply>, cancellationToken);
    }

    private async ValueTask<TReply> ExecuteAsync<TReply>(CancellationToken cancellationToken)
    {
        var packetName = ZLinkMessageNameResolver.ResolveFromMessage(request);
        var reply = await ZLinkSpotHandleRequestExecution.ExecuteAsync(
                target,
                snapshot =>
                {
                    var timeout = _timeout ??
                        runtime.Registration.ResolveMeshRequestTimeout(snapshot.RouterChannelId);
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
                        snapshot.SpotId,
                        (ulong)snapshot.Generation,
                        snapshot.NodeGeneration,
                        snapshot.AuthorityOwnerGeneration,
                        snapshot.OwnerLeaseGeneration,
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
