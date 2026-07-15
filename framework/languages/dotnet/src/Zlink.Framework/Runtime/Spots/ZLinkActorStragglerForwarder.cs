namespace Zlink.Framework.Runtime.Spots;

internal sealed class ZLinkActorStragglerForwarder
{
    private const int Capacity = 4096;
    private readonly ZLinkFrameworkRuntime _runtime;
    private readonly SemaphoreSlim _admissionSlots;
    private readonly System.Collections.Concurrent.ConcurrentDictionary<string, ActorQueue> _queues =
        new(StringComparer.Ordinal);

    public ZLinkActorStragglerForwarder(
        ZLinkFrameworkRuntime runtime,
        int capacity = Capacity)
    {
        _runtime = runtime;
        if (capacity <= 0) throw new ArgumentOutOfRangeException(nameof(capacity));
        _admissionSlots = new SemaphoreSlim(capacity, capacity);
    }

    public void Enqueue(
        ZLinkBackendActorRef sourceActor,
        ZLinkBackendActorRef targetActor,
        RoutingId sourceNodeRid,
        RoutingId sourceSessionRid,
        ulong requestId,
        uint flags,
        ZlinkStreamHeader header,
        Message body,
        ZLinkActorForwardingLease lease)
    {
        _runtime.ShutdownToken.ThrowIfCancellationRequested();
        if (!_admissionSlots.Wait(0))
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorLocationStale,
                $"Actor ref '{sourceActor.ActorId}' could not be forwarded because the relay queue is full.");
        ForwardedFrame? frame = null;
        try
        {
            frame = new ForwardedFrame(
                sourceActor,
                targetActor,
                sourceNodeRid,
                sourceSessionRid,
                requestId,
                flags,
                header,
                ZLinkStreamProtocolDefaults.EncodeHeader(header).ToArray(),
                body.ToArray(),
                lease,
                _admissionSlots);
            while (!_queues.GetOrAdd(
                       sourceActor.ActorId,
                       _ => new ActorQueue(this, sourceActor.ActorId))
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

    private async ValueTask ForwardAsync(ForwardedFrame frame, CancellationToken cancellationToken)
    {
        try
        {
            var headerSubmitted = false;
            var firstAttempt = true;
            while (firstAttempt || frame.Lease.IsActive)
            {
                firstAttempt = false;
                cancellationToken.ThrowIfCancellationRequested();
                try
                {
                    if (!headerSubmitted)
                    {
                        using var headerPart = Message.From(frame.HeaderBytes);
                        headerSubmitted = _runtime.ForwardActorBoundSessionPart(
                            frame.TargetActor,
                            frame.SourceNodeRid,
                            frame.SourceSessionRid,
                            headerPart,
                            true,
                            SendFlags.DontWait);
                        if (!headerSubmitted)
                        {
                            await DelayRetryAsync(cancellationToken).ConfigureAwait(false);
                            continue;
                        }
                    }

                    using var bodyPart = Message.From(frame.BodyBytes);
                    if (_runtime.ForwardActorBoundSessionPart(
                            frame.TargetActor,
                            frame.SourceNodeRid,
                            frame.SourceSessionRid,
                            bodyPart,
                            false,
                            SendFlags.DontWait))
                        return;
                }
                catch (ZlinkSubmitException exception)
                    when (exception.Result is ZlinkSubmitException.ErrorCode.Backpressured
                          or ZlinkSubmitException.ErrorCode.InvalidState
                          or ZlinkSubmitException.ErrorCode.NotConnected
                          or ZlinkSubmitException.ErrorCode.NotFound)
                {
                    ZLinkFrameworkDebugLog.SpotDiscovery(
                        $"straggler forward retry actor={frame.SourceActor.ActorId}: {exception.Message}");
                }
                catch (Exception exception) when (exception is not OperationCanceledException)
                {
                    ZLinkFrameworkDebugLog.SpotDiscovery(
                        $"straggler forward failed actor={frame.SourceActor.ActorId}: {exception.Message}");
                    break;
                }

                await DelayRetryAsync(cancellationToken).ConfigureAwait(false);
            }

            await ZLinkActorBoundSessionRelay.ReplyStaleActorAsync(
                    _runtime,
                    frame.SourceActor,
                    frame.SourceNodeRid,
                    frame.SourceSessionRid,
                    frame.RequestId,
                    frame.Flags,
                    frame.Header,
                    new ZLinkFrameworkException(
                        ZLinkFrameworkErrorKind.ActorLocationStale,
                        $"Actor ref '{frame.SourceActor.ActorId}' could not be forwarded before its relay window closed."),
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

    private sealed class ActorQueue(ZLinkActorStragglerForwarder owner, string actorId)
    {
        private readonly System.Collections.Concurrent.ConcurrentQueue<ForwardedFrame> _frames = new();
        private readonly object _lifecycleGate = new();
        private int _draining;
        private bool _retired;

        public bool TryEnqueue(ForwardedFrame frame)
        {
            lock (_lifecycleGate)
            {
                if (_retired) return false;
                _frames.Enqueue(frame);
                StartDrain();
                return true;
            }
        }

        private void StartDrain()
        {
            if (Interlocked.CompareExchange(ref _draining, 1, 0) != 0) return;
            if (!owner._runtime.TryRunDetached($"actor-straggler-forward:{actorId}", DrainAsync))
            {
                while (_frames.TryDequeue(out var frame))
                {
                    frame.ReleaseAdmission();
                }
                _retired = true;
                Interlocked.Exchange(ref _draining, 0);
                owner._queues.TryRemove(new KeyValuePair<string, ActorQueue>(actorId, this));
            }
        }

        private async ValueTask DrainAsync(CancellationToken cancellationToken)
        {
            try
            {
                while (_frames.TryDequeue(out var frame))
                {
                    await owner.ForwardAsync(frame, cancellationToken).ConfigureAwait(false);
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
                            frame.ReleaseAdmission();
                        }
                        _retired = true;
                        owner._queues.TryRemove(new KeyValuePair<string, ActorQueue>(actorId, this));
                    }
                    else
                    {
                        StartDrain();
                    }
                }
            }
        }
    }

    private sealed class ForwardedFrame(
        ZLinkBackendActorRef sourceActor,
        ZLinkBackendActorRef targetActor,
        RoutingId sourceNodeRid,
        RoutingId sourceSessionRid,
        ulong requestId,
        uint flags,
        ZlinkStreamHeader header,
        byte[] headerBytes,
        byte[] bodyBytes,
        ZLinkActorForwardingLease lease,
        SemaphoreSlim admissionSlots)
    {
        private int _admissionHeld = 1;

        public ZLinkBackendActorRef SourceActor { get; } = sourceActor;
        public ZLinkBackendActorRef TargetActor { get; } = targetActor;
        public RoutingId SourceNodeRid { get; } = sourceNodeRid;
        public RoutingId SourceSessionRid { get; } = sourceSessionRid;
        public ulong RequestId { get; } = requestId;
        public uint Flags { get; } = flags;
        public ZlinkStreamHeader Header { get; } = header;
        public byte[] HeaderBytes { get; } = headerBytes;
        public byte[] BodyBytes { get; } = bodyBytes;
        public ZLinkActorForwardingLease Lease { get; } = lease;

        public void ReleaseAdmission()
        {
            if (Interlocked.Exchange(ref _admissionHeld, 0) != 0)
                admissionSlots.Release();
        }
    }
}
