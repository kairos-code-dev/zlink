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
        string meshName,
        string channelName,
        TMessage message)
    {
        return new ZLinkMeshChannelSendCall<TMessage>(runtime, meshName, channelName, message);
    }

    public IZLinkRequestCall RequestToChannel<TRequest>(
        string meshName,
        string channelName,
        TRequest request)
    {
        return new ZLinkMeshChannelRequestCall<TRequest>(runtime, meshName, channelName, request);
    }

}

internal sealed class ZLinkSpotClient(ZLinkFrameworkRuntime runtime) : IZLinkSpotClient
{
    public IZLinkSendCall SendToSpot<TMessage>(SpotHandle target, TMessage message) =>
        new ZLinkRouteSpotSendCall<TMessage>(runtime, RequireResolvedHandle(target), message);

    public IZLinkRequestCall RequestToSpot<TRequest>(SpotHandle target, TRequest request) =>
        new ZLinkRouteSpotRequestCall<TRequest>(runtime, RequireResolvedHandle(target), request);

    public IZLinkSendCall SendToSpot<TMessage>(InstanceSpotAddress target, TMessage message) =>
        new ZLinkInstanceSpotSendCall<TMessage>(runtime, target, message);

    public IZLinkRequestCall RequestToSpot<TRequest>(InstanceSpotAddress target, TRequest request) =>
        new ZLinkInstanceSpotRequestCall<TRequest>(runtime, target, request);

    private static ZLinkResolvedSpotHandle RequireResolvedHandle(SpotHandle target) =>
        target as ZLinkResolvedSpotHandle
        ?? throw new ArgumentException(
            "Spot handle was not created by this framework runtime.",
            nameof(target));
}

internal sealed class ZLinkMeshChannelSendCall<TMessage>(
    ZLinkFrameworkRuntime runtime,
    string meshName,
    string channelName,
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

    public async ValueTask<ZLinkSubmitResult> SubmitAsync(
        CancellationToken cancellationToken = default)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var parts = Encode();
        return await runtime.GetMeshNodeRuntime(meshName).EntryOutbound
                .SendToChannelAsync(channelName, parts, cancellationToken, _metadata.Encode())
                .ConfigureAwait(false);
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

internal sealed class ZLinkMeshChannelRequestCall<TRequest>(
    ZLinkFrameworkRuntime runtime,
    string meshName,
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
        return _turn is null
            ? ExecuteAsync<TReply>(cancellationToken)
            : _turn.YieldFrameworkCallAsync(ExecuteAsync<TReply>, cancellationToken);
    }

    private async ValueTask<TReply> ExecuteAsync<TReply>(CancellationToken cancellationToken)
    {
        var packetName = ZLinkMessageNameResolver.ResolveFromMessage(request);
        var nodeRuntime = runtime.GetMeshNodeRuntime(meshName);
        var timeout = _timeout ?? nodeRuntime.Registration.DefaultRequestTimeout
            ?? runtime.Registration.DefaultRequestTimeout;
        var header = ZLinkClientCallCodec.CreateEnvelope(
            ZLinkMessageKind.Request,
            channelName,
            packetName,
            timeout);
        var parts = ZLinkClientCallCodec.EncodeEnvelopeParts(header, request, runtime.Registration.Codecs);
        var reply = await nodeRuntime.EntryOutbound
            .RequestToChannelAsync(channelName, parts, timeout, cancellationToken, _metadata.Encode())
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

    public async ValueTask<ZLinkSubmitResult> SubmitAsync(
        CancellationToken cancellationToken = default)
    {
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
            return await nodeRuntime
                .SendToNodeAsync(targetNodeRid, parts, cancellationToken, _metadata.Encode())
                .ConfigureAwait(false);
        }
        catch (ZLinkFrameworkException failure)
            when (ZLinkMeshCallSupport.TryMapSubmitFailure(failure, out var failed))
        {
            if (!handedOff) DisposeBeforeHandoff(parts);
            return failed;
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
        return _turn is null
            ? ExecuteAsync<TReply>(cancellationToken)
            : _turn.YieldFrameworkCallAsync(ExecuteAsync<TReply>, cancellationToken);
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
            return await runtime.SendToSpotViaRouterChannelAsync(
                    snapshot.RouterChannelId,
                    snapshot.NodeRid,
                    snapshot.SpotRid,
                    (ulong)snapshot.Generation,
                    parts,
                    cancellationToken,
                    _metadata.Encode())
                .ConfigureAwait(false);
        }
        catch (ZLinkFrameworkException failure)
            when (ZLinkMeshCallSupport.TryMapSubmitFailure(failure, out var failed))
        {
            return failed;
        }
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
