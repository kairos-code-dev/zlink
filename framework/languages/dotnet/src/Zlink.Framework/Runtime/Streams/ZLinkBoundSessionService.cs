using Systems.Zlink.Stream.Connector.Runtime;
using Zlink.Framework.Runtime.Messaging;

namespace Zlink.Framework.Runtime.Streams;

internal sealed class ZLinkBoundSessionService(
    ZLinkFrameworkRuntime runtime) : IZLinkBoundSessionService
{
    private readonly object _submitterGate = new();
    private ZLinkAsyncSubmitter? _submitter;

    public IZLinkBoundSession Create(string actorId)
    {
        return new ZLinkBoundSession(this, actorId);
    }

    internal IZLinkBoundSessionSendCall Send<TMessage>(
        string actorId,
        TMessage message)
    {
        return new ZLinkBoundSessionSendCall<TMessage>(
            this,
            actorId,
            message);
    }

    public async ValueTask DisconnectAsync(
        string actorId,
        CancellationToken cancellationToken = default)
    {
        using var operation = runtime.EnterOperation();
        if (ZLinkBoundSessionDispatchScope.TryDeferClose(
                actorId,
                ct => DisconnectNowAsync(actorId, ct)))
            return;

        await DisconnectNowAsync(actorId, cancellationToken)
            .ConfigureAwait(false);
    }

    private async ValueTask DisconnectNowAsync(
        string actorId,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        var route = ResolveSessionRoute(actorId);
        try
        {
            await runtime.CloseActorBoundSessionAsync(actorId, cancellationToken)
                .ConfigureAwait(false);
        }
        finally
        {
            if (runtime.TryGetSessionActorContext(actorId, route.BindingToken, out var context))
                runtime.UnbindSessionActor(actorId, context, route.BindingToken);

            runtime.UnbindActorSession(actorId, route.BindingToken);
        }
    }

    internal void SubmitBoundSession<TMessage>(
        string actorId,
        string? packetName,
        IReadOnlyDictionary<string, string> metadata,
        TMessage message,
        CancellationToken cancellationToken)
    {
        using var operation = runtime.EnterOperation();
        cancellationToken.ThrowIfCancellationRequested();
        _ = ResolveSessionRoute(actorId);
        using var flow = ZLinkFlowContext.EnterCurrentOrCreate(
            ZLinkFlowOrigin.Application,
            runtime.Flow.CaptureEnabled);
        var frame = CreateBoundSessionFrame(
            packetName,
            metadata,
            message,
            runtime.Registration.Codecs);
        TraceSent(actorId, packetName, frame);
        if (ZLinkBoundSessionDispatchScope.TryDefer(
                actorId,
                ct => SubmitFrameAsync(actorId, frame, ct)))
            return;

        ZLinkUnawaitedSubmit.Observe(
            SubmitFrameAsync(actorId, frame, cancellationToken),
            "actor bound-session submit",
            runtime.ErrorSink);
    }

    private void TraceSent(string actorId, string? packetName, byte[] frame)
    {
        if (!runtime.Flow.Enabled(ZLinkMessageFlowOutcome.Sent)) return;
        if (!ZLinkStreamFrameCodec.TryDecode(frame, out var headerBytes, out _))
            throw new InvalidOperationException("Actor bound session frame is invalid.");

        var header = ZLinkStreamProtocolDefaults.DecodeHeader(headerBytes.ToArray());
        runtime.Flow.Trace(new ZLinkMessageFlowEvent(
            ZLinkMessageFlowOutcome.Sent,
            ZLinkDispatchErrorSurface.StreamSession,
            ZLinkDispatchMessageKind.Send,
            packetName,
            CorrelationId: header.CorrelationId,
            ActorId: actorId));
    }

    private ValueTask SubmitFrameAsync(
        string actorId,
        byte[] frame,
        CancellationToken cancellationToken)
    {
        var message = Message.From(frame);
        return GetSubmitter().Async(
            message,
            pending => runtime.SendActorBoundSession(
                actorId,
                new[] { pending },
                SendFlags.DontWait),
            cancellationToken);
    }

    private ZLinkAsyncSubmitter GetSubmitter()
    {
        lock (_submitterGate)
            return _submitter ??= runtime.CreateActorBoundSessionSubmitter();
    }

    public ValueTask ResetAsync()
    {
        ZLinkAsyncSubmitter? submitter;
        lock (_submitterGate)
        {
            submitter = _submitter;
            _submitter = null;
        }

        return submitter?.DisposeAsync() ?? ValueTask.CompletedTask;
    }

    private ZLinkActorBoundSession ResolveSessionRoute(string actorId)
    {
        if (runtime.TryGetActorBoundSession(actorId, out var route)) return route;

        throw new ZLinkFrameworkException(
            ZLinkFrameworkErrorKind.ActorSessionNotBound,
            $"No current session binding exists for actor '{actorId}'.",
            true);
    }

    private static byte[] CreateBoundSessionFrame<TPayload>(
        string? packetName,
        IReadOnlyDictionary<string, string> metadata,
        TPayload payload,
        ZLinkCodecRegistryBuilder codecs)
    {
        var encoded = ZLinkStreamPacketPayloadCodec.Encode(payload, payload?.GetType() ?? typeof(TPayload), codecs);
        var header = new ZlinkStreamHeader(
            ZlinkStreamMessageKind.Send,
            encoded.Codec,
            MetadataFlags(metadata),
            null,
            packetName ?? throw new InvalidOperationException("Packet name is required."),
            ToStreamMetadata(metadata),
            ZlinkStreamCorrelation.Next());
        return ZLinkStreamFrameCodec.Encode(ZLinkStreamProtocolDefaults.EncodeHeader(header).Span,
            encoded.Payload.Span);
    }

    private static ZlinkStreamHeaderFlags MetadataFlags(IReadOnlyDictionary<string, string> metadata)
    {
        return metadata.Count == 0 ? ZlinkStreamHeaderFlags.None : ZlinkStreamHeaderFlags.HasMetadata;
    }

    private static ZlinkStreamMetadata ToStreamMetadata(IReadOnlyDictionary<string, string> metadata)
    {
        var values = ZlinkStreamMetadata.Empty;
        foreach (var (key, value) in metadata) values = values.With(key, value);

        return values;
    }
}

internal sealed class ZLinkBoundSession(
    ZLinkBoundSessionService service,
    string actorId) : IZLinkBoundSession
{
    public IZLinkBoundSessionSendCall Send<TMessage>(
        TMessage message)
    {
        return service.Send(actorId, message);
    }

    public ValueTask DisconnectAsync(
        CancellationToken cancellationToken = default)
    {
        return service.DisconnectAsync(actorId, cancellationToken);
    }
}

internal sealed class ZLinkBoundSessionSendCall<TMessage>(
    ZLinkBoundSessionService service,
    string actorId,
    TMessage message) : IZLinkBoundSessionSendCall
{
    private readonly Dictionary<string, string> _metadata = new(StringComparer.Ordinal);
    public IZLinkBoundSessionSendCall Metadata(
        string key,
        string value)
    {
        _metadata[key] = value;
        return this;
    }

    public void Submit(CancellationToken cancellationToken = default)
    {
        service.SubmitBoundSession(
            actorId,
            ZLinkMessageNameResolver.ResolveFromMessage(message),
            _metadata,
            message,
            cancellationToken);
    }
}
