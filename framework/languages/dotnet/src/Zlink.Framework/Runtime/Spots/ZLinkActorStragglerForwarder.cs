namespace Zlink.Framework.Runtime.Spots;

internal sealed class ZLinkActorStragglerForwarder
{
    private const int Capacity = 4096;
    private const int MappingMessageCapacity = 1024;
    private const long MappingByteCapacity = 16L * 1024 * 1024;
    private readonly ZLinkFrameworkRuntime _runtime;
    private readonly SemaphoreSlim _admissionSlots;
    private readonly System.Collections.Concurrent.ConcurrentDictionary<ForwardingKey, ActorQueue>
        _queues = new();

    public ZLinkActorStragglerForwarder(
        ZLinkFrameworkRuntime runtime,
        int capacity = Capacity)
    {
        _runtime = runtime;
        if (capacity <= 0) throw new ArgumentOutOfRangeException(nameof(capacity));
        _admissionSlots = new SemaphoreSlim(capacity, capacity);
    }

    public void Enqueue(
        ZLinkActorForwardingMapping mapping,
        RoutingId sourceNodeRid,
        RoutingId sourceSessionRid,
        ulong requestId,
        uint flags,
        ZLinkBackendActorRouteContext routeContext,
        ZlinkStreamHeader header,
        Message body)
    {
        _runtime.ShutdownToken.ThrowIfCancellationRequested();
        if (!_admissionSlots.Wait(0))
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorLocationStale,
                $"Actor ref '{mapping.SourceActor.ActorId}' could not be forwarded because the relay queue is full.");
        ForwardedFrame? frame = null;
        try
        {
            frame = new ForwardedFrame(
                mapping,
                sourceNodeRid,
                sourceSessionRid,
                requestId,
                flags,
                routeContext,
                header,
                ZLinkStreamProtocolDefaults.EncodeHeader(header).ToArray(),
                body.ToArray(),
                _admissionSlots);
            var key = new ForwardingKey(
                mapping.SourceActor.NodeRid,
                mapping.SourceActor.ActorId,
                mapping.SourceActor.Generation,
                mapping.TargetActor.NodeRid,
                mapping.TargetActor.Generation,
                mapping.SourceNodeGeneration,
                mapping.TargetNodeGeneration,
                mapping.SourceAuthorityOwnerGeneration,
                mapping.TargetAuthorityOwnerGeneration,
                mapping.SourceOwnerLeaseGeneration,
                mapping.TargetOwnerLeaseGeneration);
            while (!_queues.GetOrAdd(
                       key,
                       _ => new ActorQueue(this, key))
                   .TryEnqueue(frame))
            {
            }
        }
        catch
        {
            if (frame is null)
                _admissionSlots.Release();
            else
                frame.ReleaseAdmission();
            throw;
        }
    }

    internal static ZLinkBackendActorRouteContext AdvanceRoute(
        ZLinkActorForwardingMapping mapping,
        ZLinkBackendActorRouteContext routeContext,
        ulong requestId,
        uint flags) =>
        routeContext.IsDirectRoute
            ? new ZLinkBackendActorRouteContext(
                routeContext.OperationId,
                checked((byte)(routeContext.ForwardingHopCount + 1)),
                mapping.TargetNodeGeneration,
                mapping.TargetAuthorityOwnerGeneration,
                mapping.TargetOwnerLeaseGeneration,
                requestId,
                flags,
                routeContext.ReplyCapability)
            : routeContext.ReplyRequestId != 0
                ? new ZLinkBackendActorRouteContext(
                    default,
                    0,
                    mapping.TargetNodeGeneration,
                    mapping.TargetAuthorityOwnerGeneration,
                    mapping.TargetOwnerLeaseGeneration,
                    requestId,
                    flags,
                    routeContext.ReplyCapability)
                : default;

    private async ValueTask ForwardAsync(ForwardedFrame frame, CancellationToken cancellationToken)
    {
        try
        {
            var headerSubmitted = false;
            var firstAttempt = true;
            while (firstAttempt || frame.Mapping.Lease.IsActive)
            {
                firstAttempt = false;
                cancellationToken.ThrowIfCancellationRequested();
                try
                {
                    if (!headerSubmitted)
                    {
                        using var headerPart = Message.From(frame.HeaderBytes);
                        headerSubmitted = _runtime.ForwardActorBoundSessionPart(
                            frame.Mapping.TargetMeshName,
                            frame.Mapping.TargetActor,
                            frame.Mapping.TargetNodeGeneration,
                            frame.Mapping.TargetAuthorityOwnerGeneration,
                            frame.Mapping.TargetOwnerLeaseGeneration,
                            frame.SourceNodeRid,
                            frame.SourceSessionRid,
                            headerPart,
                            true,
                            SendFlags.DontWait,
                            frame.ForwardedRouteContext);
                        if (!headerSubmitted)
                        {
                            await DelayRetryAsync(cancellationToken).ConfigureAwait(false);
                            continue;
                        }
                    }

                    using var bodyPart = Message.From(frame.BodyBytes);
                    if (_runtime.ForwardActorBoundSessionPart(
                            frame.Mapping.TargetMeshName,
                            frame.Mapping.TargetActor,
                            frame.Mapping.TargetNodeGeneration,
                            frame.Mapping.TargetAuthorityOwnerGeneration,
                            frame.Mapping.TargetOwnerLeaseGeneration,
                            frame.SourceNodeRid,
                            frame.SourceSessionRid,
                            bodyPart,
                            false,
                            SendFlags.DontWait,
                            frame.ForwardedRouteContext))
                        return;
                }
                catch (ZlinkSubmitException exception)
                    when (exception.Result is ZlinkSubmitException.ErrorCode.Backpressured
                          or ZlinkSubmitException.ErrorCode.InvalidState
                          or ZlinkSubmitException.ErrorCode.NotConnected
                          or ZlinkSubmitException.ErrorCode.NotFound)
                {
                    ZLinkFrameworkDebugLog.SpotDiscovery(
                        $"straggler forward retry actor={frame.Mapping.SourceActor.ActorId}: {exception.Message}");
                }
                catch (Exception exception) when (exception is not OperationCanceledException)
                {
                    ZLinkFrameworkDebugLog.SpotDiscovery(
                        $"straggler forward failed actor={frame.Mapping.SourceActor.ActorId}: {exception.Message}");
                    break;
                }

                await DelayRetryAsync(cancellationToken).ConfigureAwait(false);
            }

            await ZLinkActorBoundSessionRelay.ReplyStaleActorAsync(
                    _runtime,
                    frame.Mapping.SourceActor,
                    frame.SourceNodeRid,
                    frame.SourceSessionRid,
                    frame.RequestId,
                    frame.Flags,
                    frame.ForwardedRouteContext.ReplyCapability,
                    frame.Header,
                    new ZLinkFrameworkException(
                        ZLinkFrameworkErrorKind.ActorLocationStale,
                        $"Actor ref '{frame.Mapping.SourceActor.ActorId}' could not be forwarded before its relay window closed."),
                    cancellationToken)
                .ConfigureAwait(false);
        }
        finally
        {
            frame.ReleaseAdmission();
        }
    }

    private static ValueTask DelayRetryAsync(CancellationToken cancellationToken)
        => new(Task.Delay(TimeSpan.FromMilliseconds(10), cancellationToken));

    private sealed class ActorQueue(
        ZLinkActorStragglerForwarder owner,
        ForwardingKey mapping)
    {
        private readonly System.Collections.Concurrent.ConcurrentQueue<ForwardedFrame> _frames = new();
        private readonly object _lifecycleGate = new();
        private int _draining;
        private bool _retired;
        private int _count;
        private long _bytes;

        public bool TryEnqueue(ForwardedFrame frame)
        {
            lock (_lifecycleGate)
            {
                if (_retired) return false;
                if (_count >= MappingMessageCapacity
                    || frame.EncodedSize > MappingByteCapacity - _bytes)
                    throw new ZLinkFrameworkException(
                        ZLinkFrameworkErrorKind.ActorLocationStale,
                        $"Actor ref '{mapping.ActorId}' could not be forwarded because "
                        + "its committed mapping reached the 1,024 message or 16 MiB bound.");
                _count++;
                _bytes += frame.EncodedSize;
                _frames.Enqueue(frame);
                StartDrain();
                return true;
            }
        }

        private void StartDrain()
        {
            if (Interlocked.CompareExchange(ref _draining, 1, 0) != 0) return;
            if (!owner._runtime.TryRunDetached(
                    $"actor-straggler-forward:{mapping.ActorId}",
                    DrainAsync))
            {
                while (_frames.TryDequeue(out var frame))
                {
                    ReleaseMappingAdmission(frame);
                    frame.ReleaseAdmission();
                }
                _retired = true;
                Interlocked.Exchange(ref _draining, 0);
                owner._queues.TryRemove(
                    new KeyValuePair<ForwardingKey, ActorQueue>(mapping, this));
            }
        }

        private async ValueTask DrainAsync(CancellationToken cancellationToken)
        {
            try
            {
                while (_frames.TryDequeue(out var frame))
                {
                    try
                    {
                        await owner.ForwardAsync(frame, cancellationToken).ConfigureAwait(false);
                    }
                    finally
                    {
                        ReleaseMappingAdmission(frame);
                    }
                }
            }
            finally
            {
                lock (_lifecycleGate)
                {
                    Interlocked.Exchange(ref _draining, 0);
                    if (_frames.IsEmpty || cancellationToken.IsCancellationRequested)
                    {
                        while (_frames.TryDequeue(out var frame))
                        {
                            ReleaseMappingAdmission(frame);
                            frame.ReleaseAdmission();
                        }
                        _retired = true;
                        owner._queues.TryRemove(
                            new KeyValuePair<ForwardingKey, ActorQueue>(mapping, this));
                    }
                    else
                    {
                        StartDrain();
                    }
                }
            }
        }

        private void ReleaseMappingAdmission(ForwardedFrame frame)
        {
            lock (_lifecycleGate)
            {
                _count--;
                _bytes -= frame.EncodedSize;
            }
        }
    }

    private sealed class ForwardedFrame(
        ZLinkActorForwardingMapping mapping,
        RoutingId sourceNodeRid,
        RoutingId sourceSessionRid,
        ulong requestId,
        uint flags,
        ZLinkBackendActorRouteContext routeContext,
        ZlinkStreamHeader header,
        byte[] headerBytes,
        byte[] bodyBytes,
        SemaphoreSlim admissionSlots)
    {
        private int _admissionHeld = 1;

        public ZLinkActorForwardingMapping Mapping { get; } = mapping;
        public RoutingId SourceNodeRid { get; } = sourceNodeRid;
        public RoutingId SourceSessionRid { get; } = sourceSessionRid;
        public ulong RequestId { get; } = requestId;
        public uint Flags { get; } = flags;
        public ZLinkBackendActorRouteContext ForwardedRouteContext { get; } =
            AdvanceRoute(mapping, routeContext, requestId, flags);
        public ZlinkStreamHeader Header { get; } = header;
        public byte[] HeaderBytes { get; } = headerBytes;
        public byte[] BodyBytes { get; } = bodyBytes;
        public long EncodedSize { get; } =
            checked((long)headerBytes.Length + bodyBytes.Length);
        public void ReleaseAdmission()
        {
            if (Interlocked.Exchange(ref _admissionHeld, 0) != 0)
                admissionSlots.Release();
        }
    }

    private readonly record struct ForwardingKey(
        RoutingId SourceNodeRid,
        string ActorId,
        ulong ObjectGeneration,
        RoutingId TargetNodeRid,
        ulong TargetObjectGeneration,
        ulong SourceNodeGeneration,
        ulong TargetNodeGeneration,
        ulong SourceAuthorityOwnerGeneration,
        ulong TargetAuthorityOwnerGeneration,
        ulong SourceOwnerLeaseGeneration,
        ulong TargetOwnerLeaseGeneration);
}
