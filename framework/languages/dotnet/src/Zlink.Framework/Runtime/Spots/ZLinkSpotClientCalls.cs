using Zlink.Framework.Runtime.Messaging;

namespace Zlink.Framework.Runtime.Spots;

internal sealed class ZLinkSpotOutboundService : IZLinkSpotOutbound
{
    public IZLinkSendCall SendToSpot<TMessage>(SpotHandle target, TMessage message)
    {
        return new ZLinkRoutedSpotSendCall<TMessage>(
            ZLinkSpotAmbientContext.RequireCurrent(),
            RequireResolvedHandle(target),
            message);
    }

    public IZLinkRequestCall RequestToSpot<TMessage>(SpotHandle target, TMessage request)
    {
        return new ZLinkRoutedSpotRequestCall<TMessage>(
            ZLinkSpotAmbientContext.RequireCurrent(),
            RequireResolvedHandle(target),
            request);
    }

    public IZLinkSendCall SendToSpot<TMessage>(InstanceSpotAddress target, TMessage message)
    {
        return ZLinkSpotAmbientContext.RequireCurrent().Outbound.SendToSpot(target, message);
    }

    public IZLinkRequestCall RequestToSpot<TRequest>(InstanceSpotAddress target, TRequest request)
    {
        return ZLinkSpotAmbientContext.RequireCurrent().Outbound.RequestToSpot(target, request);
    }

    public IZLinkPublishCall Publish<TEvent>(string channelName, string topic, TEvent message)
    {
        return ZLinkSpotAmbientContext.RequireCurrent().Outbound.Publish(channelName, topic, message);
    }

    public IZLinkSendCall SendToChannel<TMessage>(string channelName, TMessage message)
    {
        return ZLinkSpotAmbientContext.RequireCurrent().Outbound.SendToChannel(channelName, message);
    }

    public IZLinkRequestCall RequestToChannel<TMessage>(string channelName, TMessage request)
    {
        return ZLinkSpotAmbientContext.RequireCurrent().Outbound.RequestToChannel(channelName, request);
    }

    private static ZLinkResolvedSpotHandle RequireResolvedHandle(SpotHandle target)
    {
        return target as ZLinkResolvedSpotHandle
               ?? throw new ArgumentException("Spot handle was not created by this framework runtime.", nameof(target));
    }

}

internal sealed class ZLinkInstanceSpotSendCall<TMessage>(
    ZLinkFrameworkRuntime runtime,
    InstanceSpotAddress target,
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
        var handle = await runtime.ResolveInstanceSpotHandleAsync(target, cancellationToken)
            .ConfigureAwait(false);
        if (handle is null)
            return new ZLinkSubmitResult(ZLinkSubmitStatus.TargetNotFound);

        var call = new ZLinkRouteSpotSendCall<TMessage>(runtime, handle, message);
        call.Metadata(new ZLinkMessageMetadata(_metadata.Snapshot()));
        return await call.SubmitAsync(cancellationToken).ConfigureAwait(false);
    }
}

internal sealed class ZLinkInstanceSpotRequestCall<TRequest>(
    ZLinkFrameworkRuntime runtime,
    InstanceSpotAddress target,
    TRequest request) : IZLinkRequestCall
{
    private readonly ZLinkCallMetadata _metadata = new();
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

    public ValueTask<TReply> Async<TReply>(CancellationToken cancellationToken = default) =>
        ExecuteAsync<TReply>(cancellationToken);

    public ValueTask<TReply> Yield<TReply>(CancellationToken cancellationToken = default) =>
        ExecuteAsync<TReply>(cancellationToken);

    private async ValueTask<TReply> ExecuteAsync<TReply>(CancellationToken cancellationToken)
    {
        var handle = await runtime.ResolveInstanceSpotHandleAsync(target, cancellationToken)
            .ConfigureAwait(false)
            ?? throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.SpotRouteNotFound,
                $"Instance Spot '{target.InstanceSpotType}/{target.SpotRid}' is not ready.");
        var call = new ZLinkRouteSpotRequestCall<TRequest>(runtime, handle, request);
        if (_timeout is { } timeout) call.Timeout(timeout);
        call.Metadata(new ZLinkMessageMetadata(_metadata.Snapshot()));
        return await call.Async<TReply>(cancellationToken).ConfigureAwait(false);
    }
}

internal sealed class ZLinkRoutedSpotSendCall<TMessage>(
    IZLinkCurrentSpotActivation activation,
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
        // One-way sends use the current snapshot once and never retry; a
        // retry could duplicate a packet that was already delivered.
        var snapshot = target.Snapshot;
        var header = ZLinkClientCallCodec.CreateEnvelope(
            ZLinkMessageKind.Command,
            activation.ChannelName,
            ZLinkMessageNameResolver.ResolveFromMessage(message));
        var parts = ZLinkClientCallCodec.EncodeEnvelopeParts(header, message, activation.Codecs);
        try
        {
            return await activation.OutboundEndpoint.SendToSpotAsync(
                    snapshot.RouterChannelId,
                    snapshot.NodeRid,
                    snapshot.SpotRid,
                    (ulong)snapshot.Generation,
                    snapshot.AuthorityOwnerGeneration,
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

internal sealed class ZLinkRoutedSpotRequestCall<TRequest>(
    IZLinkCurrentSpotActivation activation,
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
        var timeout = _timeout ?? activation.DefaultRequestTimeout;
        var reply = await ZLinkSpotHandleRequestExecution.ExecuteAsync(
                target,
                snapshot =>
                {
                    var header = ZLinkClientCallCodec.CreateEnvelope(
                        ZLinkMessageKind.Request,
                        activation.ChannelName,
                        ZLinkMessageNameResolver.ResolveFromMessage(request),
                        timeout);
                    var parts = ZLinkClientCallCodec.EncodeEnvelopeParts(
                        header,
                        request,
                        activation.Codecs);
                    return activation.OutboundEndpoint.RequestToSpotAsync(
                        snapshot.RouterChannelId,
                        snapshot.NodeRid,
                        snapshot.SpotRid,
                        (ulong)snapshot.Generation,
                        snapshot.AuthorityOwnerGeneration,
                        parts,
                        timeout,
                        cancellationToken,
                        _metadata.Encode());
                },
                cancellationToken)
            .ConfigureAwait(false);
        return ZLinkClientCallCodec.DecodeEnvelopeReplyAndDispose<TReply>(
            reply,
            "SPOT request reply is empty.",
            "SPOT request failed.",
            activation.Codecs);
    }


}

internal sealed class ZLinkCurrentSpotSendCall<TMessage>(
    IZLinkCurrentSpotActivation activation,
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
        var header = ZLinkClientCallCodec.CreateEnvelope(
            ZLinkMessageKind.Command,
            channelName,
            ZLinkMessageNameResolver.ResolveFromMessage(message));
        var parts = ZLinkClientCallCodec.EncodeEnvelopeParts(header, message, activation.Codecs);
        try
        {
            return await activation.OutboundEndpoint
                .SendToChannelAsync(channelName, parts, cancellationToken, _metadata.Encode())
                .ConfigureAwait(false);
        }
        catch (ZLinkFrameworkException failure)
            when (ZLinkMeshCallSupport.TryMapSubmitFailure(failure, out var failed))
        {
            return failed;
        }
    }
}

internal sealed class ZLinkCurrentSpotRequestCall<TMessage>(
    IZLinkCurrentSpotActivation activation,
    string channelName,
    TMessage request) : IZLinkRequestCall
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
        var timeout = _timeout ?? activation.DefaultRequestTimeout;
        var header = ZLinkClientCallCodec.CreateEnvelope(
            ZLinkMessageKind.Request,
            channelName,
            ZLinkMessageNameResolver.ResolveFromMessage(request),
            timeout);
        var parts = ZLinkClientCallCodec.EncodeEnvelopeParts(header, request, activation.Codecs);
        var reply = await activation.OutboundEndpoint.RequestToChannelAsync(
            channelName,
            parts,
            timeout,
            cancellationToken,
            _metadata.Encode());
        return ZLinkClientCallCodec.DecodeEnvelopeReplyAndDispose<TReply>(
            reply,
            "SPOT channel request reply is empty.",
            "SPOT channel request failed.",
            activation.Codecs);
    }


}
