namespace Zlink.Framework.Runtime.Spots;

internal sealed class ZLinkActorMessageFollower
{
    private const int Capacity = 4096;
    private const int RouteMessageCapacity = 1024;
    private const long RouteByteCapacity = 16L * 1024 * 1024;
    private readonly ZLinkFrameworkRuntime _runtime;
    private readonly SemaphoreSlim _admissionSlots;
    private readonly System.Collections.Concurrent.ConcurrentDictionary<MessageFollowKey, ActorQueue>
        _queues = new();

    public ZLinkActorMessageFollower(
        ZLinkFrameworkRuntime runtime,
        int capacity = Capacity)
    {
        _runtime = runtime;
        if (capacity <= 0) throw new ArgumentOutOfRangeException(nameof(capacity));
        _admissionSlots = new SemaphoreSlim(capacity, capacity);
    }

    public void Enqueue(
        ZLinkActorMessageFollowRoute messageFollowRoute,
        RoutingId sourceNodeRid,
        RoutingId sourceSessionRid,
        ulong requestId,
        uint flags,
        ZLinkBackendActorRouteContext routeContext,
        ZlinkStreamHeader header,
        Message body,
        ulong sourceNodeGeneration = 0,
        ZLinkServiceWireCodec.RequestSourceFence? requestSource = null)
    {
        _runtime.ShutdownToken.ThrowIfCancellationRequested();
        if (!_admissionSlots.Wait(0))
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorLocationStale,
                $"Actor ref '{messageFollowRoute.SourceActor.ActorId}' could not use Message Follow because its queue is full.");
        MessageFollowFrame? frame = null;
        try
        {
            frame = new MessageFollowFrame(
                messageFollowRoute,
                sourceNodeRid,
                sourceSessionRid,
                requestId,
                flags,
                routeContext,
                sourceNodeGeneration,
                requestSource,
                header,
                ZLinkStreamProtocolDefaults.EncodeHeader(header).ToArray(),
                body.ToArray(),
                _admissionSlots);
            var key = new MessageFollowKey(
                messageFollowRoute.SourceActor.NodeRid,
                messageFollowRoute.SourceActor.ActorId,
                messageFollowRoute.SourceActor.Generation,
                messageFollowRoute.TargetActor.NodeRid,
                messageFollowRoute.TargetActor.Generation,
                messageFollowRoute.SourceNodeGeneration,
                messageFollowRoute.TargetNodeGeneration,
                messageFollowRoute.SourceAuthorityOwnerGeneration,
                messageFollowRoute.TargetAuthorityOwnerGeneration,
                messageFollowRoute.SourceOwnerLeaseGeneration,
                messageFollowRoute.TargetOwnerLeaseGeneration);
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
        ZLinkActorMessageFollowRoute messageFollowRoute,
        ZLinkBackendActorRouteContext routeContext,
        ulong requestId,
        uint flags) =>
        routeContext.IsDirectRoute
            ? new ZLinkBackendActorRouteContext(
                routeContext.OperationId,
                checked((byte)(routeContext.MessageFollowHopCount + 1)),
                messageFollowRoute.TargetNodeGeneration,
                messageFollowRoute.TargetAuthorityOwnerGeneration,
                messageFollowRoute.TargetOwnerLeaseGeneration,
                requestId,
                flags,
                routeContext.ReplyCapability)
            : routeContext.ReplyRequestId != 0
                ? new ZLinkBackendActorRouteContext(
                    default,
                    0,
                    messageFollowRoute.TargetNodeGeneration,
                    messageFollowRoute.TargetAuthorityOwnerGeneration,
                    messageFollowRoute.TargetOwnerLeaseGeneration,
                    requestId,
                    flags,
                    routeContext.ReplyCapability)
                : default;

    private async ValueTask FollowAsync(MessageFollowFrame frame, CancellationToken cancellationToken)
    {
        try
        {
            var headerSubmitted = false;
            var firstAttempt = true;
            while (firstAttempt || frame.MessageFollowRoute.Lease.IsActive)
            {
                firstAttempt = false;
                cancellationToken.ThrowIfCancellationRequested();
                try
                {
                    if (!headerSubmitted)
                    {
                        using var headerPart = Message.From(frame.HeaderBytes);
                        headerSubmitted = _runtime.ForwardActorBoundSessionPart(
                            frame.MessageFollowRoute.TargetMeshName,
                            frame.MessageFollowRoute.TargetActor,
                            frame.MessageFollowRoute.TargetNodeGeneration,
                            frame.MessageFollowRoute.TargetAuthorityOwnerGeneration,
                            frame.MessageFollowRoute.TargetOwnerLeaseGeneration,
                            frame.SourceNodeRid,
                            frame.SourceSessionRid,
                            headerPart,
                            true,
                            SendFlags.DontWait,
                            frame.MessageFollowRouteContext,
                            frame.SourceNodeGeneration,
                            frame.RequestSource);
                        if (!headerSubmitted)
                        {
                            await DelayRetryAsync(cancellationToken).ConfigureAwait(false);
                            continue;
                        }
                    }

                    using var bodyPart = Message.From(frame.BodyBytes);
                    if (_runtime.ForwardActorBoundSessionPart(
                            frame.MessageFollowRoute.TargetMeshName,
                            frame.MessageFollowRoute.TargetActor,
                            frame.MessageFollowRoute.TargetNodeGeneration,
                            frame.MessageFollowRoute.TargetAuthorityOwnerGeneration,
                            frame.MessageFollowRoute.TargetOwnerLeaseGeneration,
                            frame.SourceNodeRid,
                            frame.SourceSessionRid,
                            bodyPart,
                            false,
                            SendFlags.DontWait,
                            frame.MessageFollowRouteContext,
                            frame.SourceNodeGeneration,
                            frame.RequestSource))
                        return;
                }
                catch (ZlinkSubmitException exception)
                    when (exception.Result is ZlinkSubmitException.ErrorCode.Backpressured
                          or ZlinkSubmitException.ErrorCode.InvalidState
                          or ZlinkSubmitException.ErrorCode.NotConnected
                          or ZlinkSubmitException.ErrorCode.NotFound)
                {
                    ZLinkFrameworkDebugLog.SpotDiscovery(
                        $"message follow retry actor={frame.MessageFollowRoute.SourceActor.ActorId}: {exception.Message}");
                }
                catch (Exception exception) when (exception is not OperationCanceledException)
                {
                    ZLinkFrameworkDebugLog.SpotDiscovery(
                        $"message follow failed actor={frame.MessageFollowRoute.SourceActor.ActorId}: {exception.Message}");
                    break;
                }

                await DelayRetryAsync(cancellationToken).ConfigureAwait(false);
            }

            await ZLinkActorBoundSessionRelay.ReplyStaleActorAsync(
                    _runtime,
                    frame.MessageFollowRoute.SourceActor,
                    frame.SourceNodeRid,
                    frame.SourceSessionRid,
                    frame.RequestId,
                    frame.Flags,
                    frame.MessageFollowRouteContext.ReplyCapability,
                    frame.Header,
                    new ZLinkFrameworkException(
                        ZLinkFrameworkErrorKind.ActorLocationStale,
                        $"Actor ref '{frame.MessageFollowRoute.SourceActor.ActorId}' could not use Message Follow before its duration expired."),
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
        ZLinkActorMessageFollower owner,
        MessageFollowKey key)
    {
        private readonly System.Collections.Concurrent.ConcurrentQueue<MessageFollowFrame> _frames = new();
        private readonly object _lifecycleGate = new();
        private int _draining;
        private bool _retired;
        private int _count;
        private long _bytes;

        public bool TryEnqueue(MessageFollowFrame frame)
        {
            lock (_lifecycleGate)
            {
                if (_retired) return false;
                if (_count >= RouteMessageCapacity
                    || frame.EncodedSize > RouteByteCapacity - _bytes)
                    throw new ZLinkFrameworkException(
                        ZLinkFrameworkErrorKind.ActorLocationStale,
                        $"Actor ref '{key.ActorId}' could not use Message Follow because "
                        + "its Message Follow route reached the 1,024 message or 16 MiB bound.");
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
                    $"actor-message-follow:{key.ActorId}",
                    DrainAsync))
            {
                while (_frames.TryDequeue(out var frame))
                {
                    ReleaseRouteAdmission(frame);
                    frame.ReleaseAdmission();
                }
                _retired = true;
                Interlocked.Exchange(ref _draining, 0);
                owner._queues.TryRemove(
                    new KeyValuePair<MessageFollowKey, ActorQueue>(key, this));
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
                        await owner.FollowAsync(frame, cancellationToken).ConfigureAwait(false);
                    }
                    finally
                    {
                        ReleaseRouteAdmission(frame);
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
                            ReleaseRouteAdmission(frame);
                            frame.ReleaseAdmission();
                        }
                        _retired = true;
                        owner._queues.TryRemove(
                            new KeyValuePair<MessageFollowKey, ActorQueue>(key, this));
                    }
                    else
                    {
                        StartDrain();
                    }
                }
            }
        }

        private void ReleaseRouteAdmission(MessageFollowFrame frame)
        {
            lock (_lifecycleGate)
            {
                _count--;
                _bytes -= frame.EncodedSize;
            }
        }
    }

    private sealed class MessageFollowFrame(
        ZLinkActorMessageFollowRoute messageFollowRoute,
        RoutingId sourceNodeRid,
        RoutingId sourceSessionRid,
        ulong requestId,
        uint flags,
        ZLinkBackendActorRouteContext routeContext,
        ulong sourceNodeGeneration,
        ZLinkServiceWireCodec.RequestSourceFence? requestSource,
        ZlinkStreamHeader header,
        byte[] headerBytes,
        byte[] bodyBytes,
        SemaphoreSlim admissionSlots)
    {
        private int _admissionHeld = 1;

        public ZLinkActorMessageFollowRoute MessageFollowRoute { get; } = messageFollowRoute;
        public RoutingId SourceNodeRid { get; } = sourceNodeRid;
        public RoutingId SourceSessionRid { get; } = sourceSessionRid;
        public ulong RequestId { get; } = requestId;
        public uint Flags { get; } = flags;
        public ulong SourceNodeGeneration { get; } = sourceNodeGeneration;
        public ZLinkServiceWireCodec.RequestSourceFence? RequestSource { get; } =
            requestSource;
        public ZLinkBackendActorRouteContext MessageFollowRouteContext { get; } =
            AdvanceRoute(messageFollowRoute, routeContext, requestId, flags);
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

    private readonly record struct MessageFollowKey(
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
