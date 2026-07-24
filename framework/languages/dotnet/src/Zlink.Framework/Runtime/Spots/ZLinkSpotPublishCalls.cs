using Zlink.Framework.Runtime.Messaging;
using Zlink.Framework.Runtime.Execution;

namespace Zlink.Framework.Runtime.Spots;

internal sealed class ZLinkCurrentSpotPublishCall<TEvent>(
    IZLinkCurrentSpotActivation activation,
    string channelName,
    string topic,
    TEvent message) : IZLinkPublishCall
{
    private readonly ZLinkCallMetadata _metadata = new();
    private readonly string _messageName = ZLinkMessageNameResolver.ResolveFromMessage(message);

    public IZLinkPublishCall Metadata(string key, string value)
    {
        _metadata.Set(key, value);
        return this;
    }

    public IZLinkPublishCall Metadata(ZLinkMessageMetadata metadata)
    {
        _metadata.Merge(metadata);
        return this;
    }

    public async ValueTask<ZLinkPublishResult> SubmitAsync(
        CancellationToken cancellationToken = default)
    {
        using var flow = ZLinkFlowContext.EnterCurrentOrCreate(
            ZLinkFlowOrigin.Application,
            activation.Flow.CaptureEnabled);
        cancellationToken.ThrowIfCancellationRequested();
        var parts = ZLinkSpotPublishEnvelope.EncodeParts(
            channelName,
            _messageName,
            topic,
            message,
            activation.Codecs);
        if (activation.Flow.Enabled(ZLinkMessageFlowOutcome.Sent))
            activation.Flow.Trace(new ZLinkMessageFlowEvent(
                ZLinkMessageFlowOutcome.Sent,
                ZLinkDispatchErrorSurface.SpotSubscription,
                ZLinkDispatchMessageKind.Publish,
                _messageName,
                channelName,
                topic,
                SpotId: activation.SpotId.ToString()));
        try
        {
            var result = await activation.OutboundEndpoint
                .PublishCurrentAsync(channelName, topic, parts, cancellationToken, _metadata.Encode())
                .ConfigureAwait(false);
            ZLinkRuntimeMetrics.RecordFanoutPublished(null);
            return result.ToPublishResult();
        }
        finally
        {
            ZLinkMessageParts.DisposeAll(parts);
        }
    }
}

internal sealed class ZLinkSpotPublisherClientService(ZLinkFrameworkRuntime runtime)
    : IZLinkSpotPublisherClient
{
    public IZLinkPublishCall Publish<TEvent>(
        string meshName,
        string channelName,
        string topic,
        TEvent message)
    {
        return new ZLinkExternalSpotPublishCall<TEvent>(
            runtime, meshName, channelName, topic, message);
    }
}

internal sealed class ZLinkExternalSpotPublishCall<TEvent>(
    ZLinkFrameworkRuntime runtime,
    string meshName,
    string channelName,
    string topic,
    TEvent message) : IZLinkPublishCall
{
    private readonly ZLinkCallMetadata _metadata = new();
    private readonly string _messageName = ZLinkMessageNameResolver.ResolveFromMessage(message);

    public IZLinkPublishCall Metadata(string key, string value)
    {
        _metadata.Set(key, value);
        return this;
    }

    public IZLinkPublishCall Metadata(ZLinkMessageMetadata metadata)
    {
        _metadata.Merge(metadata);
        return this;
    }

    public async ValueTask<ZLinkPublishResult> SubmitAsync(
        CancellationToken cancellationToken = default)
    {
        using var operation = runtime.EnterOperation();
        using var flow = ZLinkFlowContext.EnterCurrentOrCreate(
            ZLinkFlowOrigin.Application,
            runtime.Flow.CaptureEnabled);
        var bundle = runtime.GetSpotPublisherBundle(meshName);
        var packetName = _messageName;
        var parts = ZLinkSpotPublishEnvelope.EncodeParts(
            channelName,
            packetName,
            topic,
            message,
            runtime.Registration.Codecs);
        var metadata = _metadata.Encode();

        if (runtime.Flow.Enabled(ZLinkMessageFlowOutcome.Sent))
            runtime.Flow.Trace(new ZLinkMessageFlowEvent(
                ZLinkMessageFlowOutcome.Sent,
                ZLinkDispatchErrorSurface.SpotSubscription,
                ZLinkDispatchMessageKind.Publish,
                packetName,
                channelName,
                topic,
                SpotId: bundle.Spot.RoutingId.ToString()));

        try
        {
            var result = await ZLinkLogicalMulticastSubmitter.SubmitAsync(
                    runtime.WorkerPool,
                    () => bundle.Spot.Publish(
                        channelName,
                        topic,
                        parts,
                        SendFlags.None,
                        metadata),
                    cancellationToken,
                    runtime.ShutdownToken)
                .ConfigureAwait(false);
            ZLinkRuntimeMetrics.RecordFanoutPublished(null);
            return result.ToPublishResult();
        }
        finally
        {
            ZLinkMessageParts.DisposeAll(parts);
        }
    }
}

internal static class ZLinkLogicalMulticastSubmitter
{
    private static readonly MeshPublishDetail EmptyDetail = new(0, 0, 0, 0, 0, 0, 0);

    public static ValueTask<MeshPublishResult> SubmitAsync(
        ZLinkWorkerPool workerPool,
        Func<MeshPublishResult> publish,
        CancellationToken cancellationToken,
        CancellationToken shutdownToken)
    {
        ArgumentNullException.ThrowIfNull(workerPool);
        ArgumentNullException.ThrowIfNull(publish);
        cancellationToken.ThrowIfCancellationRequested();
        if (shutdownToken.IsCancellationRequested)
            return ValueTask.FromResult(new MeshPublishResult(SubmitResult.Terminated, EmptyDetail));

        var operation = new LogicalMulticastOperation(
            publish,
            cancellationToken,
            shutdownToken);
        var admission = workerPool.TrySubmitDirect(
            operation.Run,
            operation.FailShutdownBeforeStart);
        operation.ResolveAdmission(admission);
        return new ValueTask<MeshPublishResult>(operation.Task);
    }

    private sealed class LogicalMulticastOperation
    {
        private const int Pending = 0;
        private const int Committed = 1;
        private const int Terminal = 2;

        private readonly CancellationToken _cancellationToken;
        private readonly Func<MeshPublishResult> _publish;
        private readonly CancellationToken _shutdownToken;
        private readonly TaskCompletionSource<MeshPublishResult> _source = new(
            TaskCreationOptions.RunContinuationsAsynchronously);
        private CancellationTokenRegistration _cancellationRegistration;
        private CancellationTokenRegistration _shutdownRegistration;
        private int _state;

        public LogicalMulticastOperation(
            Func<MeshPublishResult> publish,
            CancellationToken cancellationToken,
            CancellationToken shutdownToken)
        {
            _publish = publish;
            _cancellationToken = cancellationToken;
            _shutdownToken = shutdownToken;
            _cancellationRegistration = cancellationToken.Register(
                static state => ((LogicalMulticastOperation)state!).CancelBeforeCommit(),
                this);
            _shutdownRegistration = shutdownToken.Register(
                static state => ((LogicalMulticastOperation)state!).FailShutdownBeforeStart(),
                this);
            if (Volatile.Read(ref _state) == Terminal) DisposeRegistrations();
        }

        public Task<MeshPublishResult> Task => _source.Task;

        public void ResolveAdmission(ZLinkWorkerSubmitResult admission)
        {
            if (admission == ZLinkWorkerSubmitResult.Accepted) return;
            var result = admission == ZLinkWorkerSubmitResult.Full
                ? SubmitResult.Backpressured
                : SubmitResult.Terminated;
            CompletePending(new MeshPublishResult(result, EmptyDetail));
        }

        public void Run(CancellationToken shutdownToken)
        {
            _ = shutdownToken;
            if (Interlocked.CompareExchange(ref _state, Committed, Pending) != Pending) return;
            DisposeRegistrations();
            try
            {
                _source.TrySetResult(_publish());
            }
            catch (Exception error)
            {
                _source.TrySetException(error);
            }
            finally
            {
                Volatile.Write(ref _state, Terminal);
            }
        }

        public void FailShutdownBeforeStart()
        {
            CompletePending(new MeshPublishResult(SubmitResult.Terminated, EmptyDetail));
        }

        private void CancelBeforeCommit()
        {
            if (Interlocked.CompareExchange(ref _state, Terminal, Pending) != Pending) return;
            _source.TrySetCanceled(_cancellationToken);
            DisposeRegistrations();
        }

        private void CompletePending(MeshPublishResult result)
        {
            if (Interlocked.CompareExchange(ref _state, Terminal, Pending) != Pending) return;
            _source.TrySetResult(result);
            DisposeRegistrations();
        }

        private void DisposeRegistrations()
        {
            _cancellationRegistration.Dispose();
            _shutdownRegistration.Dispose();
        }
    }
}
internal static class ZLinkPublishResultMapper
{
    /// <summary>Maps a Core submit failure onto the public status contract;
    /// unknown codes stay exceptions.</summary>
    public static bool TryMapSubmitFailure(
        ZlinkSubmitException.ErrorCode code, out ZLinkPublishResult result)
    {
        ZLinkSubmitStatus? status = code switch
        {
            ZlinkSubmitException.ErrorCode.Backpressured => ZLinkSubmitStatus.Backpressured,
            ZlinkSubmitException.ErrorCode.NotFound => ZLinkSubmitStatus.TargetNotFound,
            ZlinkSubmitException.ErrorCode.NotConnected => ZLinkSubmitStatus.RouteNotConnected,
            ZlinkSubmitException.ErrorCode.Terminated => ZLinkSubmitStatus.Shutdown,
            _ => null
        };
        result = status is { } mapped
            ? new ZLinkPublishResult(mapped, default)
            : default;
        return status is not null;
    }

    public static ZLinkPublishResult ToPublishResult(this MeshPublishResult result)
    {
        var status = result.Result switch
        {
            SubmitResult.Ok => ZLinkSubmitStatus.Submitted,
            SubmitResult.Backpressured => ZLinkSubmitStatus.Backpressured,
            SubmitResult.NotFound => ZLinkSubmitStatus.TargetNotFound,
            SubmitResult.NotConnected or SubmitResult.NotAdmitted =>
                ZLinkSubmitStatus.RouteNotConnected,
            SubmitResult.Terminated => ZLinkSubmitStatus.Shutdown,
            _ => throw new InvalidOperationException(
                $"Logical Multicast failed with submit result '{result.Result}'.")
        };
        var detail = result.Detail;
        return new ZLinkPublishResult(
            status,
            new ZLinkLogicalMulticastDetail(
                detail.SnapshotRemoteTargets,
                detail.AdmittedRemoteTargets,
                detail.DroppedRemoteTargets,
                detail.UnreachableRemoteTargets,
                detail.SnapshotLocalSpots,
                detail.AdmittedLocalSpots,
                detail.DroppedLocalSpots));
    }
}


internal static class ZLinkSpotPublishEnvelope
{
    public static IReadOnlyList<Message> EncodeParts<TEvent>(
        string channelName,
        string messageName,
        string topic,
        TEvent message,
        ZLinkCodecRegistryBuilder? codecs = null,
        string? correlationId = null)
    {
        var header = new ZLinkEnvelopeHeader(
            ZLinkMessageKind.Publish,
            channelName,
            messageName,
            ZLinkEnvelopeCodec.DefaultContentType,
            correlationId,
            null,
            topic,
            null,
            null,
            channelName);
        return ZLinkEnvelopeCodec.EncodeParts(
            header,
            message,
            message?.GetType() ?? typeof(TEvent),
            codecs);
    }
}
