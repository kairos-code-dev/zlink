namespace Zlink.Framework.Runtime.Actors;

internal sealed class ZLinkActorHandoffState(
    string actorId,
    TimeProvider timeProvider,
    Action<string>? diagnostic = null)
{
    private readonly object _gate = new();
    private readonly object _forwardGate = new();
    private readonly List<ZLinkActorHandoffFrame> _frames = [];
    private CancellationTokenSource? _forwardingExpiry;
    private ZLinkActorForwardingMapping? _forwarding;
    private ZLinkBackendActorRef? _staleSourceActor;
    private string? _handoffId;
    private ZLinkRemoteActorJoinRequest? _joinRequest;
    private ZLinkActorSourceHandoffPhase _sourcePhase;
    private ZLinkActorTargetHandoffPhase _targetPhase;
    private long _arrivalIndex;
    private int _importedFrameCount;
    private bool _sourceTrailingImported;
    private TaskCompletionSource<ZLinkRemoteActorJoinReply>? _preparation;
    private TaskCompletionSource? _sourceCompletion;

    public void BeginCapture()
    {
        lock (_gate)
        {
            if (_sourcePhase is ZLinkActorSourceHandoffPhase.Capturing
                or ZLinkActorSourceHandoffPhase.CutoverPending
                or ZLinkActorSourceHandoffPhase.ForwardingCommitted
                || _targetPhase is ZLinkActorTargetHandoffPhase.Importing
                    or ZLinkActorTargetHandoffPhase.Replaying
                    or ZLinkActorTargetHandoffPhase.Quarantined)
                throw new InvalidOperationException(
                    $"Actor '{actorId}' already has an active handoff transaction.");

            _handoffId = null;
            _joinRequest = null;
            _preparation = null;
            _targetPhase = ZLinkActorTargetHandoffPhase.Idle;
            _sourceTrailingImported = false;
            _sourcePhase = ZLinkActorSourceHandoffPhase.Capturing;
            _sourceCompletion = new TaskCompletionSource(
                TaskCreationOptions.RunContinuationsAsynchronously);
            _frames.Clear();
            _arrivalIndex = 0;
        }
    }

    public bool IsSourceMigrationInProgress
    {
        get
        {
            lock (_gate)
                return _sourcePhase is ZLinkActorSourceHandoffPhase.Capturing
                    or ZLinkActorSourceHandoffPhase.CutoverPending
                    or ZLinkActorSourceHandoffPhase.ForwardingCommitted;
        }
    }

    public bool RetainsSourceTombstone
    {
        get
        {
            lock (_gate) return _staleSourceActor is not null;
        }
    }

    public bool BlocksLocalDispatch
    {
        get
        {
            lock (_gate)
                return _sourcePhase is ZLinkActorSourceHandoffPhase.Capturing
                           or ZLinkActorSourceHandoffPhase.CutoverPending
                           or ZLinkActorSourceHandoffPhase.ForwardingCommitted
                       || _targetPhase is ZLinkActorTargetHandoffPhase.Importing
                           or ZLinkActorTargetHandoffPhase.AuthorityCommitted
                           or ZLinkActorTargetHandoffPhase.NotifyingJoined
                           or ZLinkActorTargetHandoffPhase.Prepared
                           or ZLinkActorTargetHandoffPhase.Replaying
                           or ZLinkActorTargetHandoffPhase.Quarantined;
        }
    }

    public void CompleteSourceMigration()
    {
        TaskCompletionSource? completion;
        lock (_gate)
        {
            if (_sourcePhase is not (ZLinkActorSourceHandoffPhase.CutoverPending
                or ZLinkActorSourceHandoffPhase.ForwardingCommitted))
                throw new InvalidOperationException(
                    $"Actor '{actorId}' does not have a source migration to complete.");
            _sourcePhase = ZLinkActorSourceHandoffPhase.Retired;
            completion = _sourceCompletion;
            _sourceCompletion = null;
        }
        completion?.TrySetResult();
    }

    public Task WaitForSourceCompletionAsync(CancellationToken cancellationToken)
    {
        Task completion;
        lock (_gate)
        {
            if (_sourcePhase is not (ZLinkActorSourceHandoffPhase.Capturing
                or ZLinkActorSourceHandoffPhase.CutoverPending
                or ZLinkActorSourceHandoffPhase.ForwardingCommitted))
                return Task.CompletedTask;
            completion = (_sourceCompletion ??= new TaskCompletionSource(
                TaskCreationOptions.RunContinuationsAsynchronously)).Task;
        }
        return completion.WaitAsync(cancellationToken);
    }

    public bool TryCapture(ZLinkSpotActorFrame frame)
    {
        lock (_gate)
        {
            if (_sourcePhase != ZLinkActorSourceHandoffPhase.Capturing
                && _targetPhase is not (ZLinkActorTargetHandoffPhase.Importing
                    or ZLinkActorTargetHandoffPhase.AuthorityCommitted
                    or ZLinkActorTargetHandoffPhase.Replaying))
                return false;

            _frames.Add(ZLinkActorHandoffFrames.Capture(frame, _arrivalIndex++));
            diagnostic?.Invoke(
                $"handoff_backlog actor={actorId} arrival={_arrivalIndex - 1} kind={frame.Header.Kind} request_id={frame.RequestId} flags={frame.Flags}");
            return true;
        }
    }

    public bool Import(
        ZLinkRemoteActorJoinRequest request,
        out Task<ZLinkRemoteActorJoinReply> preparation)
    {
        var handoffId = request.HandoffId;
        if (string.IsNullOrWhiteSpace(handoffId))
            throw new InvalidOperationException("Actor handoff id must not be empty.");

        lock (_gate)
        {
            if (_sourcePhase is ZLinkActorSourceHandoffPhase.Capturing
                or ZLinkActorSourceHandoffPhase.CutoverPending
                or ZLinkActorSourceHandoffPhase.ForwardingCommitted)
                throw new InvalidOperationException(
                    $"Actor '{actorId}' cannot import while its source handoff is active.");
            if (string.Equals(_handoffId, handoffId, StringComparison.Ordinal))
            {
                if (_joinRequest is null
                    || !ZLinkActorHandoffRequestIdentity.Matches(_joinRequest, request))
                    throw new InvalidOperationException(
                        $"Actor '{actorId}' handoff '{handoffId}' was retried with different commit data.");
                preparation = _preparation?.Task
                              ?? throw new InvalidOperationException(
                                  $"Actor '{actorId}' handoff '{handoffId}' has no preparation result.");
                return false;
            }
            if (_targetPhase is ZLinkActorTargetHandoffPhase.Importing
                or ZLinkActorTargetHandoffPhase.AuthorityCommitted
                or ZLinkActorTargetHandoffPhase.NotifyingJoined
                or ZLinkActorTargetHandoffPhase.Prepared
                or ZLinkActorTargetHandoffPhase.Replaying
                or ZLinkActorTargetHandoffPhase.Quarantined)
                throw new InvalidOperationException(
                    $"Actor '{actorId}' already has an active handoff '{_handoffId}'.");

            _handoffId = handoffId;
            _joinRequest = request;
            _preparation = new TaskCompletionSource<ZLinkRemoteActorJoinReply>(
                TaskCreationOptions.RunContinuationsAsynchronously);
            preparation = _preparation.Task;
            _targetPhase = ZLinkActorTargetHandoffPhase.Importing;
            _frames.Clear();
            _arrivalIndex = 0;
            foreach (var frame in request.HandoffFrames.OrderBy(static frame => frame.ArrivalIndex))
            {
                _frames.Add(frame with { ArrivalIndex = _arrivalIndex++ });
                diagnostic?.Invoke(
                    $"backlog_enqueued actor={actorId} arrival={_arrivalIndex - 1} request_id={frame.RequestId} flags={frame.Flags}");
            }
            _importedFrameCount = _frames.Count;
            _sourceTrailingImported = false;
            return true;
        }
    }

    internal void BeginCanonicalMaintenanceImport(
        string handoffId,
        IReadOnlyList<ZLinkActorHandoffFrame> frames)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(handoffId);
        ArgumentNullException.ThrowIfNull(frames);
        lock (_gate)
        {
            if (string.Equals(_handoffId, handoffId, StringComparison.Ordinal)
                && _targetPhase is ZLinkActorTargetHandoffPhase.Importing
                    or ZLinkActorTargetHandoffPhase.AuthorityCommitted
                    or ZLinkActorTargetHandoffPhase.Replaying
                    or ZLinkActorTargetHandoffPhase.Completed)
                return;
            if (_sourcePhase is ZLinkActorSourceHandoffPhase.Capturing
                    or ZLinkActorSourceHandoffPhase.CutoverPending
                    or ZLinkActorSourceHandoffPhase.ForwardingCommitted
                || _targetPhase is ZLinkActorTargetHandoffPhase.Importing
                    or ZLinkActorTargetHandoffPhase.AuthorityCommitted
                    or ZLinkActorTargetHandoffPhase.NotifyingJoined
                    or ZLinkActorTargetHandoffPhase.Prepared
                    or ZLinkActorTargetHandoffPhase.Replaying
                    or ZLinkActorTargetHandoffPhase.Quarantined)
                throw new InvalidOperationException(
                    $"Actor '{actorId}' already has an active handoff transaction.");
            _handoffId = handoffId;
            _joinRequest = null;
            _preparation = null;
            _targetPhase = ZLinkActorTargetHandoffPhase.Importing;
            _frames.Clear();
            _arrivalIndex = 0;
            foreach (var frame in frames.OrderBy(static frame => frame.ArrivalIndex))
                _frames.Add(frame with { ArrivalIndex = _arrivalIndex++ });
            _importedFrameCount = _frames.Count;
            _sourceTrailingImported = true;
        }
    }

    public void AcceptPreparation(string handoffId, ZLinkRemoteActorJoinReply reply)
    {
        lock (_gate)
        {
            if (!string.Equals(_handoffId, handoffId, StringComparison.Ordinal))
                throw new InvalidOperationException(
                    $"Actor '{actorId}' cannot accept an inactive handoff preparation.");
            if (_targetPhase != ZLinkActorTargetHandoffPhase.NotifyingJoined)
                throw new InvalidOperationException(
                    $"Actor '{actorId}' handoff target notification is not active.");
            _targetPhase = ZLinkActorTargetHandoffPhase.Prepared;
            _preparation!.TrySetResult(reply);
        }
    }

    public void MarkAuthorityCommitted(
        string handoffId,
        ulong sourceObjectGeneration,
        ulong targetObjectGeneration)
    {
        if (sourceObjectGeneration == 0
            || targetObjectGeneration != sourceObjectGeneration)
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.ActorGenerationStale,
                $"Actor '{actorId}' target changed ObjectGeneration during handoff.");

        lock (_gate)
        {
            if (!string.Equals(_handoffId, handoffId, StringComparison.Ordinal)
                || _targetPhase != ZLinkActorTargetHandoffPhase.Importing)
                throw new InvalidOperationException(
                    $"Actor '{actorId}' cannot commit authority for an inactive handoff.");
            _targetPhase = ZLinkActorTargetHandoffPhase.AuthorityCommitted;
        }
    }

    public bool IsAuthorityCommitted(string handoffId)
    {
        lock (_gate)
            return string.Equals(_handoffId, handoffId, StringComparison.Ordinal)
                   && _targetPhase is ZLinkActorTargetHandoffPhase.AuthorityCommitted
                       or ZLinkActorTargetHandoffPhase.NotifyingJoined
                       or ZLinkActorTargetHandoffPhase.Prepared
                       or ZLinkActorTargetHandoffPhase.Replaying
                       or ZLinkActorTargetHandoffPhase.Completed;
    }

    public bool TryBeginJoinedNotification(string handoffId)
    {
        lock (_gate)
        {
            if (!string.Equals(_handoffId, handoffId, StringComparison.Ordinal))
                throw new InvalidOperationException(
                    $"Actor '{actorId}' cannot notify an inactive handoff.");
            if (_targetPhase != ZLinkActorTargetHandoffPhase.AuthorityCommitted)
                return false;
            _targetPhase = ZLinkActorTargetHandoffPhase.NotifyingJoined;
            return true;
        }
    }

    public void RetryJoinedNotification(string handoffId)
    {
        lock (_gate)
        {
            if (!string.Equals(_handoffId, handoffId, StringComparison.Ordinal)
                || _targetPhase != ZLinkActorTargetHandoffPhase.NotifyingJoined)
                return;
            _targetPhase = ZLinkActorTargetHandoffPhase.AuthorityCommitted;
        }
    }

    public void RejectPreparation(string handoffId, ZLinkRemoteActorJoinReply reply)
    {
        lock (_gate)
        {
            if (!string.Equals(_handoffId, handoffId, StringComparison.Ordinal)) return;
            if (_targetPhase is not (ZLinkActorTargetHandoffPhase.Importing
                or ZLinkActorTargetHandoffPhase.AuthorityCommitted
                or ZLinkActorTargetHandoffPhase.NotifyingJoined
                or ZLinkActorTargetHandoffPhase.Prepared
                or ZLinkActorTargetHandoffPhase.Replaying
                or ZLinkActorTargetHandoffPhase.Quarantined))
                return;
            _preparation?.TrySetResult(reply);
        }
    }

    public bool IsKnown(string handoffId)
    {
        lock (_gate) return string.Equals(_handoffId, handoffId, StringComparison.Ordinal);
    }

    public bool IsQuarantined(string handoffId)
    {
        lock (_gate)
            return _targetPhase == ZLinkActorTargetHandoffPhase.Quarantined
                   && string.Equals(_handoffId, handoffId, StringComparison.Ordinal);
    }

    public void Quarantine(string handoffId)
    {
        lock (_gate)
        {
            if (string.Equals(_handoffId, handoffId, StringComparison.Ordinal))
                _targetPhase = ZLinkActorTargetHandoffPhase.Quarantined;
        }
    }

    public void AbortImport(string handoffId)
    {
        lock (_gate)
        {
            if (!string.Equals(_handoffId, handoffId, StringComparison.Ordinal)) return;
            _frames.Clear();
            _importedFrameCount = 0;
            _sourceTrailingImported = false;
            _handoffId = null;
            _joinRequest = null;
            _targetPhase = ZLinkActorTargetHandoffPhase.RolledBack;
            _preparation = null;
        }
    }

    public void Complete(string handoffId)
    {
        lock (_gate)
        {
            if (!string.Equals(_handoffId, handoffId, StringComparison.Ordinal))
                throw new InvalidOperationException(
                    $"Actor '{actorId}' cannot complete an inactive handoff.");
            if (_targetPhase != ZLinkActorTargetHandoffPhase.Replaying
                || _frames.Count != 0)
                throw new InvalidOperationException(
                    $"Actor '{actorId}' cannot complete before target replay drains.");
            _targetPhase = ZLinkActorTargetHandoffPhase.Completed;
            _sourceTrailingImported = false;
        }
    }

    public IReadOnlyList<ZLinkActorHandoffFrame> SnapshotFrames()
    {
        lock (_gate)
        {
            if (_sourcePhase != ZLinkActorSourceHandoffPhase.Capturing)
                throw new InvalidOperationException(
                    $"Actor '{actorId}' source handoff capture is not active.");
            return _frames.ToArray();
        }
    }

    public IReadOnlyList<ZLinkActorHandoffFrame> CutoverCaptureToForwarding(
        int committedFrameCount,
        ZLinkBackendActorRef sourceActor,
        ZLinkBackendActorRef targetActor,
        string targetMeshName,
        ulong sourceNodeGeneration,
        ulong targetNodeGeneration,
        ulong sourceAuthorityOwnerGeneration,
        ulong targetAuthorityOwnerGeneration,
        ulong sourceOwnerLeaseGeneration,
        ulong targetOwnerLeaseGeneration)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(targetMeshName);
        if (sourceNodeGeneration == 0
            || targetNodeGeneration == 0
            || sourceAuthorityOwnerGeneration == 0
            || targetAuthorityOwnerGeneration
               != checked(sourceAuthorityOwnerGeneration + 1)
            || sourceOwnerLeaseGeneration == 0
            || targetOwnerLeaseGeneration == 0)
            throw new ArgumentOutOfRangeException(
                nameof(targetAuthorityOwnerGeneration),
                "Actor forwarding requires an exact committed source-to-target authority fence.");
        lock (_forwardGate)
        {
            lock (_gate)
            {
                if (_sourcePhase != ZLinkActorSourceHandoffPhase.Capturing)
                    throw new InvalidOperationException(
                        $"Actor '{actorId}' does not have an active source handoff capture.");
                if (committedFrameCount < 0 || committedFrameCount > _frames.Count)
                    throw new ArgumentOutOfRangeException(nameof(committedFrameCount));

                _forwardingExpiry?.Cancel();
                _forwardingExpiry = null;
                _forwarding = new ZLinkActorForwardingMapping(
                    sourceActor,
                    targetActor,
                    targetMeshName,
                    sourceNodeGeneration,
                    targetNodeGeneration,
                    sourceAuthorityOwnerGeneration,
                    targetAuthorityOwnerGeneration,
                    sourceOwnerLeaseGeneration,
                    targetOwnerLeaseGeneration,
                    new ZLinkActorForwardingWindow(timeProvider));
                _staleSourceActor = sourceActor;
                _sourcePhase = ZLinkActorSourceHandoffPhase.CutoverPending;
                diagnostic?.Invoke(
                    $"mapping_installed actor={actorId} source={sourceActor.NodeRid} target={targetActor.NodeRid} entries=1");
                return _frames.Skip(committedFrameCount).ToArray();
            }
        }
    }

    public void CommitForwardingCutover(TimeSpan window)
    {
        CancellationTokenSource expiry;
        lock (_forwardGate)
        {
            lock (_gate)
            {
                if (_sourcePhase != ZLinkActorSourceHandoffPhase.CutoverPending
                    || _forwarding is not { } forwarding)
                    throw new InvalidOperationException(
                        $"Actor '{actorId}' does not have a pending forwarding cutover.");

                _frames.Clear();
                _sourcePhase = ZLinkActorSourceHandoffPhase.ForwardingCommitted;
                expiry = new CancellationTokenSource();
                _forwardingExpiry = expiry;
                forwarding.Lease.Commit(window);
            }
        }

        _ = EvictForwardingMappingAsync(window, expiry);
    }

    public IReadOnlyList<ZLinkActorHandoffFrame> PrepareImportedReplay(
        IReadOnlyList<ZLinkActorHandoffFrame> sourceTrailingFrames)
    {
        lock (_gate)
        {
            if (_targetPhase is not (ZLinkActorTargetHandoffPhase.Importing
                or ZLinkActorTargetHandoffPhase.Prepared
                or ZLinkActorTargetHandoffPhase.Replaying))
                throw new InvalidOperationException(
                    $"Actor '{actorId}' does not have a target handoff to replay.");
            _targetPhase = ZLinkActorTargetHandoffPhase.Replaying;
            if (!_sourceTrailingImported)
            {
                var trailing = sourceTrailingFrames
                    .OrderBy(static frame => frame.ArrivalIndex)
                    .ToArray();
                var trailingStart = _importedFrameCount;
                _frames.InsertRange(_importedFrameCount, trailing);
                for (var index = 0; index < _frames.Count; index++)
                    _frames[index] = _frames[index] with { ArrivalIndex = index };
                for (var index = trailingStart; index < _frames.Count; index++)
                {
                    var frame = _frames[index];
                    diagnostic?.Invoke(
                        $"backlog_enqueued actor={actorId} arrival={frame.ArrivalIndex} request_id={frame.RequestId} flags={frame.Flags}");
                }
                _arrivalIndex = _frames.Count;
                _sourceTrailingImported = true;
                _importedFrameCount = 0;
            }

            return _frames.ToArray();
        }
    }

    internal IReadOnlyList<ZLinkActorHandoffFrame>
        PrepareCanonicalMaintenanceReplay(string handoffId)
    {
        lock (_gate)
        {
            if (!string.Equals(_handoffId, handoffId, StringComparison.Ordinal)
                || _targetPhase is not (
                    ZLinkActorTargetHandoffPhase.AuthorityCommitted
                    or ZLinkActorTargetHandoffPhase.Replaying))
                throw new InvalidOperationException(
                    $"Actor '{actorId}' has no committed canonical maintenance import.");
            _targetPhase = ZLinkActorTargetHandoffPhase.Replaying;
            return _frames.ToArray();
        }
    }

    public IReadOnlyList<ZLinkActorHandoffFrame> SnapshotFinalReplay()
    {
        lock (_gate)
        {
            if (_targetPhase != ZLinkActorTargetHandoffPhase.Replaying)
                throw new InvalidOperationException(
                    $"Actor '{actorId}' target handoff replay is not active.");
            return _frames.ToArray();
        }
    }

    public void AcknowledgeReplayedFrame()
    {
        lock (_gate)
        {
            if (_targetPhase != ZLinkActorTargetHandoffPhase.Replaying)
                throw new InvalidOperationException(
                    $"Actor '{actorId}' cannot acknowledge replay outside target replay.");
            if (_frames.Count == 0)
                throw new InvalidOperationException(
                    $"Actor '{actorId}' does not have a handoff frame to acknowledge.");
            _frames.RemoveAt(0);
        }
    }

    public IReadOnlyList<ZLinkActorHandoffFrame> AbortCapture()
    {
        TaskCompletionSource? completion;
        lock (_forwardGate)
        {
            lock (_gate)
            {
                if (_sourcePhase is not (ZLinkActorSourceHandoffPhase.Capturing
                    or ZLinkActorSourceHandoffPhase.CutoverPending))
                    throw new InvalidOperationException(
                        $"Actor '{actorId}' does not have an abortable source handoff.");
                _sourcePhase = ZLinkActorSourceHandoffPhase.Idle;
                var frames = _frames.ToArray();
                _frames.Clear();
                _importedFrameCount = 0;
                _sourceTrailingImported = false;
                _staleSourceActor = null;
                completion = _sourceCompletion;
                _sourceCompletion = null;
                ClearForwardingMappingLocked();
                completion?.TrySetResult();
                return frames;
            }
        }
    }

    public ZLinkActorFrameRoute ResolveFrameRoute(
        ZLinkBackendActorRef? currentActor,
        ZLinkBackendActorRef frameActor,
        out ZLinkBackendActorRef targetActor)
    {
        lock (_gate)
        {
            return ResolveFrameRouteLocked(currentActor, frameActor, out targetActor);
        }
    }

    public ZLinkActorFrameRoute RouteFrame(
        ZLinkBackendActorRef? currentActor,
        ZLinkBackendActorRef frameActor,
        out ZLinkActorForwardingMapping? forwarding)
    {
        lock (_forwardGate)
        {
            lock (_gate)
            {
                var targetActor = default(ZLinkBackendActorRef);
                var route = ResolveFrameRouteLocked(currentActor, frameActor, out targetActor);
                forwarding = route == ZLinkActorFrameRoute.Forward
                    ? _forwarding
                    : null;
                return route;
            }
        }
    }

    public void Reset()
    {
        lock (_forwardGate)
        {
            lock (_gate)
            {
                _sourcePhase = ZLinkActorSourceHandoffPhase.Idle;
                _targetPhase = ZLinkActorTargetHandoffPhase.Idle;
                _frames.Clear();
                _importedFrameCount = 0;
                _sourceTrailingImported = false;
                _handoffId = null;
                _joinRequest = null;
                _preparation = null;
                _sourceCompletion?.TrySetResult();
                _sourceCompletion = null;
                _staleSourceActor = null;
                ClearForwardingMappingLocked();
            }
        }
    }

    public void AbortRuntimeGeneration(Exception failure)
    {
        ArgumentNullException.ThrowIfNull(failure);
        TaskCompletionSource<ZLinkRemoteActorJoinReply>? preparation;
        lock (_forwardGate)
        {
            lock (_gate)
            {
                preparation = _preparation;
                _sourcePhase = ZLinkActorSourceHandoffPhase.Idle;
                _targetPhase = ZLinkActorTargetHandoffPhase.Idle;
                _frames.Clear();
                _importedFrameCount = 0;
                _sourceTrailingImported = false;
                _handoffId = null;
                _joinRequest = null;
                _preparation = null;
                _sourceCompletion?.TrySetException(failure);
                _sourceCompletion = null;
                _staleSourceActor = null;
                ClearForwardingMappingLocked();
            }
        }

        preparation?.TrySetException(failure);
    }

    private ZLinkActorFrameRoute ResolveFrameRouteLocked(
        ZLinkBackendActorRef? currentActor,
        ZLinkBackendActorRef frameActor,
        out ZLinkBackendActorRef targetActor)
    {
        targetActor = currentActor ?? frameActor;
        if (_targetPhase == ZLinkActorTargetHandoffPhase.Quarantined)
            return ZLinkActorFrameRoute.Stale;
        if (_forwarding is { } forwarding)
        {
            if (!forwarding.Lease.IsActive)
            {
                ClearForwardingMappingLocked();
            }
            else if (forwarding.SourceActor.NodeRid == frameActor.NodeRid
                     && forwarding.SourceActor.Generation == frameActor.Generation)
            {
                targetActor = forwarding.TargetActor;
                return ZLinkActorFrameRoute.Forward;
            }
        }

        if (_staleSourceActor is { } stale
            && stale.NodeRid == frameActor.NodeRid
            && stale.Generation == frameActor.Generation)
            return ZLinkActorFrameRoute.Stale;

        if (targetActor.NodeRid == frameActor.NodeRid
            && targetActor.Generation == frameActor.Generation)
            return ZLinkActorFrameRoute.Current;

        return ZLinkActorFrameRoute.Stale;
    }

    private async Task EvictForwardingMappingAsync(
        TimeSpan window,
        CancellationTokenSource expiry)
    {
        try
        {
            await Task.Delay(window, timeProvider, expiry.Token).ConfigureAwait(false);
            lock (_forwardGate)
            {
                lock (_gate)
                {
                    if (!ReferenceEquals(_forwardingExpiry, expiry)) return;

                    ClearForwardingMappingLocked();
                    diagnostic?.Invoke($"mapping_evicted actor={actorId} entries=0");
                }
            }
        }
        catch (OperationCanceledException) when (expiry.IsCancellationRequested)
        {
        }
        finally
        {
            expiry.Dispose();
        }
    }

    private void ClearForwardingMappingLocked()
    {
        _forwarding?.Lease.Cancel();
        _forwarding = null;
        var expiry = _forwardingExpiry;
        _forwardingExpiry = null;
        expiry?.Cancel();
    }
}

internal readonly record struct ZLinkActorForwardingMapping(
    ZLinkBackendActorRef SourceActor,
    ZLinkBackendActorRef TargetActor,
    string TargetMeshName,
    ulong SourceNodeGeneration,
    ulong TargetNodeGeneration,
    ulong SourceAuthorityOwnerGeneration,
    ulong TargetAuthorityOwnerGeneration,
    ulong SourceOwnerLeaseGeneration,
    ulong TargetOwnerLeaseGeneration,
    ZLinkActorForwardingWindow Lease);

internal sealed class ZLinkActorForwardingWindow(TimeProvider timeProvider)
{
    private readonly object _gate = new();
    private ZLinkActorForwardingWindowPhase _phase;
    private long _committedAt;
    private TimeSpan _window;

    public bool IsActive
    {
        get
        {
            lock (_gate)
            {
                return _phase switch
                {
                    ZLinkActorForwardingWindowPhase.Pending => true,
                    ZLinkActorForwardingWindowPhase.Committed =>
                        timeProvider.GetElapsedTime(_committedAt) < _window,
                    _ => false
                };
            }
        }
    }

    public bool IsCommitted
    {
        get
        {
            lock (_gate)
                return _phase == ZLinkActorForwardingWindowPhase.Committed
                       && timeProvider.GetElapsedTime(_committedAt) < _window;
        }
    }

    public void Commit(TimeSpan window)
    {
        lock (_gate)
        {
            if (_phase != ZLinkActorForwardingWindowPhase.Pending)
                throw new InvalidOperationException("Actor forwarding lease is not pending.");
            _committedAt = timeProvider.GetTimestamp();
            _window = window;
            _phase = ZLinkActorForwardingWindowPhase.Committed;
        }
    }

    public void Cancel()
    {
        lock (_gate) _phase = ZLinkActorForwardingWindowPhase.Cancelled;
    }
}

internal enum ZLinkActorForwardingWindowPhase
{
    Pending,
    Committed,
    Cancelled
}

internal enum ZLinkActorSourceHandoffPhase
{
    Idle,
    Capturing,
    CutoverPending,
    ForwardingCommitted,
    Retired
}

internal enum ZLinkActorTargetHandoffPhase
{
    Idle,
    Importing,
    AuthorityCommitted,
    NotifyingJoined,
    Prepared,
    Replaying,
    Completed,
    Quarantined,
    RolledBack
}
