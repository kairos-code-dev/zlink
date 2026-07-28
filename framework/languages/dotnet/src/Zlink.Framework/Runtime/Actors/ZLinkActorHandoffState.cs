namespace Zlink.Framework.Runtime.Actors;

internal sealed class ZLinkActorHandoffState(
    string actorId,
    TimeProvider timeProvider,
    Action<string>? diagnostic = null,
    ZLinkBoundedIngressAdmission? sourceIngressAdmission = null,
    ZLinkBoundedIngressAdmission? targetIngressAdmission = null,
    ZLinkBoundedIngressAdmission? sourceHoldAdmission = null)
{
    private readonly object _gate = new();
    private readonly object _messageFollowGate = new();
    private readonly List<ZLinkActorHandoffFrame> _frames = [];
    private readonly List<ZLinkActorHandoffFrame> _sourceHoldFrames = [];
    private readonly ZLinkBoundedIngressAdmission _sourceIngressAdmission =
        sourceIngressAdmission ?? new ZLinkBoundedIngressAdmission();
    private readonly ZLinkBoundedIngressAdmission _sourceHoldAdmission =
        sourceHoldAdmission ?? new ZLinkBoundedIngressAdmission();
    private ZLinkBoundedIngressAdmission _targetIngressAdmission =
        targetIngressAdmission ?? new ZLinkBoundedIngressAdmission();
    private CancellationTokenSource? _messageFollowExpiry;
    private ZLinkActorMessageFollowRoute? _messageFollowRoute;
    private ZLinkBackendActorRef? _staleSourceActor;
    private string? _handoffId;
    private ZLinkRemoteActorJoinRequest? _joinRequest;
    private ZLinkActorSourceHandoffPhase _sourcePhase;
    private ZLinkActorTargetHandoffPhase _targetPhase;
    private long _arrivalIndex;
    private int _importedFrameCount;
    private int _sourceCommittedFrameCount = -1;
    private int _sourceCommittedHoldCount;
    private bool _sourceTrailingImported;
    private bool _sourceCaptureSealed;
    private bool _abortRestoreAdmissionsReleased;
    private TaskCompletionSource<ZLinkRemoteActorJoinReply>? _preparation;
    private TaskCompletionSource? _sourceCompletion;
    private Task? _canonicalMaintenanceDrain;

    public void BeginCapture()
    {
        lock (_gate)
        {
            if (_sourcePhase is ZLinkActorSourceHandoffPhase.Capturing
                or ZLinkActorSourceHandoffPhase.CutoverPending
                or ZLinkActorSourceHandoffPhase.AbortRestoring
                or ZLinkActorSourceHandoffPhase.MessageFollowCommitted
                || _targetPhase is ZLinkActorTargetHandoffPhase.Importing
                    or ZLinkActorTargetHandoffPhase.Replaying
                    or ZLinkActorTargetHandoffPhase.AdmissionOpenDraining
                    or ZLinkActorTargetHandoffPhase.Quarantined)
                throw new InvalidOperationException(
                    $"Actor '{actorId}' already has an active handoff transaction.");

            _handoffId = null;
            _joinRequest = null;
            _preparation = null;
            _canonicalMaintenanceDrain = null;
            _targetPhase = ZLinkActorTargetHandoffPhase.Idle;
            _sourceTrailingImported = false;
            _sourceCaptureSealed = false;
            _sourceCommittedFrameCount = -1;
            _sourceCommittedHoldCount = 0;
            _abortRestoreAdmissionsReleased = false;
            _sourcePhase = ZLinkActorSourceHandoffPhase.Capturing;
            _sourceCompletion = new TaskCompletionSource(
                TaskCreationOptions.RunContinuationsAsynchronously);
            _sourceIngressAdmission.ReleaseAll();
            _sourceHoldAdmission.ReleaseAll();
            _targetIngressAdmission.ReleaseAll();
            _frames.Clear();
            _sourceHoldFrames.Clear();
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
                    or ZLinkActorSourceHandoffPhase.AbortRestoring
                    or ZLinkActorSourceHandoffPhase.MessageFollowCommitted;
        }
    }

    internal bool CapturesSourceIngress
    {
        get
        {
            lock (_gate)
                return _sourcePhase == ZLinkActorSourceHandoffPhase.Capturing;
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
                           or ZLinkActorSourceHandoffPhase.AbortRestoring
                           or ZLinkActorSourceHandoffPhase.MessageFollowCommitted
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
                or ZLinkActorSourceHandoffPhase.MessageFollowCommitted))
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
                or ZLinkActorSourceHandoffPhase.MessageFollowCommitted))
                return Task.CompletedTask;
            completion = (_sourceCompletion ??= new TaskCompletionSource(
                TaskCreationOptions.RunContinuationsAsynchronously)).Task;
        }
        return completion.WaitAsync(cancellationToken);
    }

    public ZLinkActorHandoffCaptureResult TryCapture(
        ZLinkSpotActorFrame frame,
        Action? prepareCapture = null)
    {
        lock (_gate)
        {
            var capturesSourceIngress =
                _sourcePhase == ZLinkActorSourceHandoffPhase.Capturing;
            var capturesSourceHold = capturesSourceIngress
                                     && _sourceCaptureSealed;
            var capturesTargetIngress =
                _targetPhase is ZLinkActorTargetHandoffPhase.Importing
                    or ZLinkActorTargetHandoffPhase.AuthorityCommitted
                    or ZLinkActorTargetHandoffPhase.NotifyingJoined
                    or ZLinkActorTargetHandoffPhase.Prepared
                    or ZLinkActorTargetHandoffPhase.Replaying;
            if (!capturesSourceIngress
                && !capturesTargetIngress)
                return ZLinkActorHandoffCaptureResult.NotSealed;

            if (!frame.RouteContext.IsDirectRoute)
            {
                if (frame.RequestSource is not null
                    || frame.SourceNodeGeneration != 0)
                    throw new ZLinkActorHandoffRejectedException(
                        $"Actor '{actorId}' received a bound-session frame with "
                        + "a synthetic request-source fence.");
                // Bound-session work has no lease-backed source identity and
                // therefore cannot enter the durable relocation journal. The
                // inbound pipeline gives requests an explicit ActorMoving
                // terminal and discards one-way sends at this sealed boundary.
                return ZLinkActorHandoffCaptureResult.NotSealed;
            }

            if (frame.RequestSource is not { } source
                || frame.SourceNodeGeneration == 0
                || frame.SourceNodeRid != source.NodeRid
                || frame.SourceNodeGeneration != source.NodeGeneration)
                throw new ZLinkActorHandoffRejectedException(
                    $"Actor '{actorId}' cannot accept a direct request without "
                    + "an exact ingress request-source fence.");

            prepareCapture?.Invoke();
            var captured = ZLinkActorHandoffFrames.Capture(frame, _arrivalIndex);
            var encodedBytes = capturesSourceIngress || capturesTargetIngress
                ? ZLinkActorHandoffFrames.CanonicalEncodedLength(
                    captured,
                    frame.Actor)
                : 0;
            if (capturesSourceIngress || capturesTargetIngress)
                captured = captured with
                {
                    CanonicalEncodedLength = encodedBytes
                };
            if (capturesSourceIngress
                && !capturesSourceHold
                && !_sourceIngressAdmission.TryAcquire(encodedBytes))
                return ZLinkActorHandoffCaptureResult.Full;
            if (capturesSourceHold
                && !_sourceHoldAdmission.TryAcquire(encodedBytes))
                return ZLinkActorHandoffCaptureResult.Full;
            if (capturesTargetIngress
                && !_targetIngressAdmission.TryAcquire(encodedBytes))
                return ZLinkActorHandoffCaptureResult.Full;
            try
            {
                if (capturesSourceHold)
                    _sourceHoldFrames.Add(captured);
                else
                    _frames.Add(captured);
                _arrivalIndex++;
            }
            catch
            {
                if (capturesSourceIngress && !capturesSourceHold)
                    _sourceIngressAdmission.Release(encodedBytes);
                if (capturesSourceHold)
                    _sourceHoldAdmission.Release(encodedBytes);
                if (capturesTargetIngress)
                    _targetIngressAdmission.Release(encodedBytes);
                throw;
            }
            diagnostic?.Invoke(
                $"handoff_backlog actor={actorId} arrival={_arrivalIndex - 1} kind={frame.Header.Kind} request_id={frame.RequestId} flags={frame.Flags}");
            return ZLinkActorHandoffCaptureResult.Captured;
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
                or ZLinkActorSourceHandoffPhase.MessageFollowCommitted)
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
                or ZLinkActorTargetHandoffPhase.AdmissionOpenDraining
                or ZLinkActorTargetHandoffPhase.Quarantined)
                throw new InvalidOperationException(
                    $"Actor '{actorId}' already has an active handoff '{_handoffId}'.");

            _handoffId = handoffId;
            _joinRequest = request;
            _preparation = new TaskCompletionSource<ZLinkRemoteActorJoinReply>(
                TaskCreationOptions.RunContinuationsAsynchronously);
            preparation = _preparation.Task;
            _targetPhase = ZLinkActorTargetHandoffPhase.Importing;
            _sourceIngressAdmission.ReleaseAll();
            _sourceHoldAdmission.ReleaseAll();
            _targetIngressAdmission.ReleaseAll();
            _frames.Clear();
            _sourceHoldFrames.Clear();
            _arrivalIndex = 0;
            foreach (var frame in request.HandoffFrames.OrderBy(static frame => frame.ArrivalIndex))
            {
                _frames.Add(frame with
                {
                    ArrivalIndex = _arrivalIndex++,
                    CanonicalEncodedLength = 0
                });
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
        IReadOnlyList<ZLinkActorHandoffFrame> frames,
        int? negotiatedMessages = null,
        long? negotiatedBytes = null)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(handoffId);
        ArgumentNullException.ThrowIfNull(frames);
        lock (_gate)
        {
            if (string.Equals(_handoffId, handoffId, StringComparison.Ordinal)
                && _targetPhase is ZLinkActorTargetHandoffPhase.Importing
                    or ZLinkActorTargetHandoffPhase.AuthorityCommitted
                    or ZLinkActorTargetHandoffPhase.Replaying
                    or ZLinkActorTargetHandoffPhase.AdmissionOpenDraining
                    or ZLinkActorTargetHandoffPhase.Completed)
                return;
            if (_sourcePhase is ZLinkActorSourceHandoffPhase.Capturing
                    or ZLinkActorSourceHandoffPhase.CutoverPending
                    or ZLinkActorSourceHandoffPhase.MessageFollowCommitted
                || _targetPhase is ZLinkActorTargetHandoffPhase.Importing
                    or ZLinkActorTargetHandoffPhase.AuthorityCommitted
                    or ZLinkActorTargetHandoffPhase.NotifyingJoined
                    or ZLinkActorTargetHandoffPhase.Prepared
                    or ZLinkActorTargetHandoffPhase.Replaying
                    or ZLinkActorTargetHandoffPhase.AdmissionOpenDraining
                    or ZLinkActorTargetHandoffPhase.Quarantined)
                throw new InvalidOperationException(
                    $"Actor '{actorId}' already has an active handoff transaction.");
            _handoffId = handoffId;
            _joinRequest = null;
            _preparation = null;
            _canonicalMaintenanceDrain = null;
            _targetPhase = ZLinkActorTargetHandoffPhase.Importing;
            _sourceIngressAdmission.ReleaseAll();
            _sourceHoldAdmission.ReleaseAll();
            _targetIngressAdmission.ReleaseAll();
            if (negotiatedMessages is not null || negotiatedBytes is not null)
            {
                if (negotiatedMessages is null || negotiatedBytes is null)
                    throw new ArgumentException(
                        "Canonical target admission requires both negotiated bounds.");
                _targetIngressAdmission = new ZLinkBoundedIngressAdmission(
                    negotiatedMessages.Value,
                    negotiatedBytes.Value);
            }
            _frames.Clear();
            _sourceHoldFrames.Clear();
            long previousSequence = 0;
            try
            {
                foreach (var frame in frames.OrderBy(
                             static frame => frame.ArrivalIndex))
                {
                    if (frame.ArrivalIndex <= previousSequence
                        || frame.CanonicalEncodedLength <= 0)
                        throw new ZLinkRelocationDataLostException(
                            $"Actor '{actorId}' canonical accepted sequence or size is invalid.");
                    if (!_targetIngressAdmission.TryAcquire(
                            frame.CanonicalEncodedLength))
                        throw new ZLinkFrameworkException(
                            ZLinkFrameworkErrorKind.Unavailable,
                            $"Actor '{actorId}' canonical target backlog exceeds its bound.",
                            ZLinkRetryAdvice.RetryAfterBackoff);
                    _frames.Add(frame);
                    previousSequence = frame.ArrivalIndex;
                }
            }
            catch
            {
                _targetIngressAdmission.ReleaseAll();
                _frames.Clear();
                _handoffId = null;
                _targetPhase = ZLinkActorTargetHandoffPhase.RolledBack;
                throw;
            }
            _arrivalIndex = checked(previousSequence + 1);
            _importedFrameCount = _frames.Count;
            _sourceTrailingImported = true;
        }
    }

    internal void AppendCanonicalMaintenanceImport(
        string handoffId,
        IReadOnlyList<ZLinkActorHandoffFrame> frames)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(handoffId);
        ArgumentNullException.ThrowIfNull(frames);
        lock (_gate)
        {
            if (!string.Equals(_handoffId, handoffId, StringComparison.Ordinal)
                || _targetPhase != ZLinkActorTargetHandoffPhase.Importing)
                throw new InvalidOperationException(
                    $"Actor '{actorId}' does not have the matching canonical import.");
            var ordered = frames.OrderBy(static frame => frame.ArrivalIndex)
                .ToArray();
            var expectedSequence = _arrivalIndex;
            long requiredBytes = 0;
            foreach (var frame in ordered)
            {
                if (frame.ArrivalIndex != expectedSequence
                    || frame.CanonicalEncodedLength <= 0)
                    throw new ZLinkRelocationDataLostException(
                        $"Actor '{actorId}' canonical delta sequence or size is invalid.");
                expectedSequence++;
                requiredBytes = checked(requiredBytes
                    + frame.CanonicalEncodedLength);
            }
            if (ordered.Length
                    > _targetIngressAdmission.RemainingRecordCapacity
                || requiredBytes
                    > _targetIngressAdmission.RemainingByteCapacity)
                throw new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.Unavailable,
                    $"Actor '{actorId}' canonical target backlog exceeds its negotiated bound.",
                    ZLinkRetryAdvice.RetryAfterBackoff);
            foreach (var frame in ordered)
            {
                if (!_targetIngressAdmission.TryAcquire(frame.CanonicalEncodedLength))
                    throw new ZLinkFrameworkException(
                        ZLinkFrameworkErrorKind.Unavailable,
                        $"Actor '{actorId}' canonical target backlog exceeds its negotiated bound.",
                        ZLinkRetryAdvice.RetryAfterBackoff);
                _frames.Add(frame);
                _arrivalIndex++;
            }
            _importedFrameCount = _frames.Count;
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
                ZLinkFrameworkErrorKind.InvalidOperation,
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
                       or ZLinkActorTargetHandoffPhase.AdmissionOpenDraining
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
                or ZLinkActorTargetHandoffPhase.AdmissionOpenDraining
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
            _sourceIngressAdmission.ReleaseAll();
            _sourceHoldAdmission.ReleaseAll();
            _targetIngressAdmission.ReleaseAll();
            _frames.Clear();
            _sourceHoldFrames.Clear();
            _importedFrameCount = 0;
            _sourceTrailingImported = false;
            _handoffId = null;
            _joinRequest = null;
            _targetPhase = ZLinkActorTargetHandoffPhase.RolledBack;
            _preparation = null;
            _canonicalMaintenanceDrain = null;
        }
    }

    public void Complete(string handoffId)
    {
        lock (_gate)
        {
            if (!string.Equals(_handoffId, handoffId, StringComparison.Ordinal))
                throw new InvalidOperationException(
                    $"Actor '{actorId}' cannot complete an inactive handoff.");
            if (_targetPhase == ZLinkActorTargetHandoffPhase.Completed)
            {
                _canonicalMaintenanceDrain = null;
                return;
            }
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

    internal void SealCapture()
    {
        lock (_gate)
        {
            if (_sourcePhase != ZLinkActorSourceHandoffPhase.Capturing
                || _sourceCaptureSealed)
                throw new InvalidOperationException(
                    $"Actor '{actorId}' source handoff capture cannot be sealed.");
            _sourceCaptureSealed = true;
        }
    }

    internal ZLinkActorHandoffCommitBoundary FreezeCaptureCommitBoundary()
    {
        lock (_gate)
        {
            if (_sourcePhase != ZLinkActorSourceHandoffPhase.Capturing
                || !_sourceCaptureSealed
                || _sourceCommittedFrameCount >= 0)
                throw new InvalidOperationException(
                    $"Actor '{actorId}' source commit boundary cannot be frozen.");
            var frames = _frames
                .Concat(_sourceHoldFrames)
                .OrderBy(static frame => frame.ArrivalIndex)
                .ToArray();
            _sourceCommittedFrameCount = frames.Length;
            _sourceCommittedHoldCount = _sourceHoldFrames.Count;
            return new ZLinkActorHandoffCommitBoundary(
                frames,
                checked((ulong)frames.Length),
                _sourceHoldAdmission.RemainingRecordCapacity,
                _sourceHoldAdmission.RemainingByteCapacity);
        }
    }

    public IReadOnlyList<ZLinkActorHandoffFrame> CutoverCaptureToMessageFollow(
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
            || sourceAuthorityOwnerGeneration > long.MaxValue
            || targetAuthorityOwnerGeneration
               is 0 or > long.MaxValue
            || targetAuthorityOwnerGeneration <= sourceAuthorityOwnerGeneration
            || sourceOwnerLeaseGeneration == 0
            || targetOwnerLeaseGeneration == 0)
            throw new ArgumentOutOfRangeException(
                nameof(targetAuthorityOwnerGeneration),
                "Actor Message Follow requires an exact committed source-to-target authority fence.");
        lock (_messageFollowGate)
        {
            lock (_gate)
            {
                if (_sourcePhase != ZLinkActorSourceHandoffPhase.Capturing)
                    throw new InvalidOperationException(
                        $"Actor '{actorId}' does not have an active source handoff capture.");
                if (committedFrameCount < 0
                    || (!_sourceCaptureSealed
                        && committedFrameCount > _frames.Count)
                    || (_sourceCaptureSealed
                        && (_sourceCommittedFrameCount < 0
                            || committedFrameCount
                            != _sourceCommittedFrameCount)))
                    throw new ArgumentOutOfRangeException(nameof(committedFrameCount));

                _messageFollowExpiry?.Cancel();
                _messageFollowExpiry = null;
                _messageFollowRoute = new ZLinkActorMessageFollowRoute(
                    sourceActor,
                    targetActor,
                    targetMeshName,
                    sourceNodeGeneration,
                    targetNodeGeneration,
                    sourceAuthorityOwnerGeneration,
                    targetAuthorityOwnerGeneration,
                    sourceOwnerLeaseGeneration,
                    targetOwnerLeaseGeneration,
                    new ZLinkActorMessageFollowLease(timeProvider));
                _staleSourceActor = sourceActor;
                _sourcePhase = ZLinkActorSourceHandoffPhase.CutoverPending;
                _sourceCaptureSealed = true;
                _sourceIngressAdmission.ReleaseAll();
                _sourceHoldAdmission.ReleaseAll();
                diagnostic?.Invoke(
                    $"message_follow_registered source_rid={sourceActor.NodeRid} target_rid={targetActor.NodeRid} entries=1");
                return _frames
                    .Skip(Math.Min(committedFrameCount, _frames.Count))
                    .Concat(_sourceHoldFrames.Skip(_sourceCommittedHoldCount))
                    .OrderBy(static frame => frame.ArrivalIndex)
                    .ToArray();
            }
        }
    }

    public void CommitMessageFollow(TimeSpan duration)
    {
        CancellationTokenSource expiry;
        lock (_messageFollowGate)
        {
            lock (_gate)
            {
                if (_sourcePhase != ZLinkActorSourceHandoffPhase.CutoverPending
                    || _messageFollowRoute is not { } messageFollowRoute)
                    throw new InvalidOperationException(
                        $"Actor '{actorId}' does not have a pending Message Follow commit.");

                _sourceIngressAdmission.ReleaseAll();
                _sourceHoldAdmission.ReleaseAll();
                _targetIngressAdmission.ReleaseAll();
                _frames.Clear();
                _sourceHoldFrames.Clear();
                _sourcePhase = ZLinkActorSourceHandoffPhase.MessageFollowCommitted;
                _sourceCaptureSealed = true;
                expiry = new CancellationTokenSource();
                _messageFollowExpiry = expiry;
                messageFollowRoute.Lease.Commit(duration);
            }
        }

        _ = RemoveMessageFollowRouteAfterDelayAsync(duration, expiry);
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
                    .Select(static frame => frame with
                    {
                        CanonicalEncodedLength = 0
                    })
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

    internal bool IsCanonicalMaintenanceReplayComplete(string handoffId)
    {
        lock (_gate)
            return string.Equals(_handoffId, handoffId,
                       StringComparison.Ordinal)
                   && _targetPhase == ZLinkActorTargetHandoffPhase.Completed;
    }

    internal bool TryOpenCanonicalMaintenanceAdmission(
        string handoffId,
        long queuedThroughArrivalIndex)
    {
        lock (_gate)
        {
            if (!string.Equals(_handoffId, handoffId,
                    StringComparison.Ordinal)
                || _targetPhase != ZLinkActorTargetHandoffPhase.Replaying)
                return false;
            if (_frames.Any(frame =>
                    frame.ArrivalIndex > queuedThroughArrivalIndex))
                return false;
            _targetPhase =
                ZLinkActorTargetHandoffPhase.AdmissionOpenDraining;
            return true;
        }
    }

    internal void ReserveCanonicalMaintenanceTrailingAndOpenAdmission(
            string handoffId,
            long queuedThroughArrivalIndex,
            Action<ZLinkActorHandoffFrame> reserveReplay)
    {
        ArgumentNullException.ThrowIfNull(reserveReplay);
        lock (_gate)
        {
            if (!string.Equals(_handoffId, handoffId,
                    StringComparison.Ordinal)
                || _targetPhase != ZLinkActorTargetHandoffPhase.Replaying)
                throw new InvalidOperationException(
                    $"Actor '{actorId}' cannot open canonical maintenance admission.");
            var trailing = _frames
                .Where(frame =>
                    frame.ArrivalIndex > queuedThroughArrivalIndex)
                // Frames relayed by the previous owner were accepted before
                // the authority cutover. They must reserve execution before
                // direct ingress that observed the new owner, even when the
                // network delivers the direct frame first.
                .OrderBy(static frame =>
                    frame.RouteContext.MessageFollowHopCount == 0 ? 1 : 0)
                .ThenBy(static frame => frame.ArrivalIndex)
                .ToArray();
            foreach (var frame in trailing)
                reserveReplay(frame);
            // New target ingress can bypass capture only after every
            // preserved frame has reserved its mailbox position.
            _targetPhase =
                ZLinkActorTargetHandoffPhase.AdmissionOpenDraining;
        }
    }

    internal void RegisterCanonicalMaintenanceDrain(
        string handoffId,
        Task drain)
    {
        ArgumentNullException.ThrowIfNull(drain);
        lock (_gate)
        {
            if (!string.Equals(_handoffId, handoffId,
                    StringComparison.Ordinal)
                || _targetPhase
                   != ZLinkActorTargetHandoffPhase.AdmissionOpenDraining
                || _canonicalMaintenanceDrain is not null)
                throw new InvalidOperationException(
                    $"Actor '{actorId}' cannot register the canonical drain.");
            _canonicalMaintenanceDrain = drain;
        }
    }

    internal Task? GetCanonicalMaintenanceDrain(string handoffId)
    {
        lock (_gate)
            return string.Equals(_handoffId, handoffId,
                StringComparison.Ordinal)
                ? _canonicalMaintenanceDrain
                : null;
    }

    internal bool TryCompleteCanonicalMaintenanceReplay(string handoffId)
    {
        lock (_gate)
        {
            if (!string.Equals(_handoffId, handoffId,
                    StringComparison.Ordinal)
                || _targetPhase
                   != ZLinkActorTargetHandoffPhase.AdmissionOpenDraining
                || _frames.Count != 0)
                return false;
            _targetPhase = ZLinkActorTargetHandoffPhase.Completed;
            _sourceTrailingImported = false;
            return true;
        }
    }

    public IReadOnlyList<ZLinkActorHandoffFrame> SnapshotFinalReplay()
    {
        lock (_gate)
        {
            if (_targetPhase is not (
                    ZLinkActorTargetHandoffPhase.Replaying
                    or ZLinkActorTargetHandoffPhase.AdmissionOpenDraining))
                throw new InvalidOperationException(
                    $"Actor '{actorId}' target handoff replay is not active.");
            return _frames.ToArray();
        }
    }

    public void AcknowledgeReplayedFrame()
    {
        lock (_gate)
        {
            if (_targetPhase is not (
                    ZLinkActorTargetHandoffPhase.Replaying
                    or ZLinkActorTargetHandoffPhase.AdmissionOpenDraining))
                throw new InvalidOperationException(
                    $"Actor '{actorId}' cannot acknowledge replay outside target replay.");
            if (_frames.Count == 0)
                throw new InvalidOperationException(
                    $"Actor '{actorId}' does not have a handoff frame to acknowledge.");
            var removed = _frames[0];
            _frames.RemoveAt(0);
            if (removed.CanonicalEncodedLength > 0)
                _targetIngressAdmission.Release(
                    removed.CanonicalEncodedLength);
        }
    }

    internal void AcknowledgeReplayedFrame(long arrivalIndex)
    {
        lock (_gate)
        {
            if (_targetPhase is not (
                    ZLinkActorTargetHandoffPhase.Replaying
                    or ZLinkActorTargetHandoffPhase.AdmissionOpenDraining))
                throw new InvalidOperationException(
                    $"Actor '{actorId}' cannot acknowledge replay outside target replay.");
            var index = _frames.FindIndex(frame =>
                frame.ArrivalIndex == arrivalIndex);
            if (index < 0)
                throw new InvalidOperationException(
                    $"Actor '{actorId}' does not have replay frame '{arrivalIndex}' to acknowledge.");
            var removed = _frames[index];
            _frames.RemoveAt(index);
            if (removed.CanonicalEncodedLength > 0)
                _targetIngressAdmission.Release(
                    removed.CanonicalEncodedLength);
        }
    }

    internal void AcknowledgeCanonicalReplayThrough(ulong acceptedSequence)
    {
        lock (_gate)
        {
            if (_targetPhase is not (
                    ZLinkActorTargetHandoffPhase.Replaying
                    or ZLinkActorTargetHandoffPhase.AdmissionOpenDraining))
                throw new InvalidOperationException(
                    $"Actor '{actorId}' cannot acknowledge canonical replay outside target replay.");
            while (_frames.Count != 0
                   && checked((ulong)_frames[0].ArrivalIndex) <= acceptedSequence)
            {
                var removed = _frames[0];
                _frames.RemoveAt(0);
                _targetIngressAdmission.Release(
                    removed.CanonicalEncodedLength);
            }
        }
    }

    public IReadOnlyList<ZLinkActorHandoffFrame> AbortCapture()
    {
        var frames = BeginAbortCaptureRestore();
        foreach (var frame in frames)
            AcknowledgeAbortRestoreEnqueued(frame.ArrivalIndex);
        CompleteAbortCaptureRestore();
        return frames;
    }

    internal IReadOnlyList<ZLinkActorHandoffFrame> BeginAbortCaptureRestore()
    {
        lock (_messageFollowGate)
        {
            lock (_gate)
            {
                if (_sourcePhase is not (ZLinkActorSourceHandoffPhase.Capturing
                    or ZLinkActorSourceHandoffPhase.CutoverPending
                    or ZLinkActorSourceHandoffPhase.AbortRestoring))
                    throw new InvalidOperationException(
                        $"Actor '{actorId}' does not have an abortable source handoff.");
                if (_sourcePhase != ZLinkActorSourceHandoffPhase.AbortRestoring)
                    _abortRestoreAdmissionsReleased =
                        _sourcePhase == ZLinkActorSourceHandoffPhase.CutoverPending;
                _sourcePhase = ZLinkActorSourceHandoffPhase.AbortRestoring;
                return _frames
                    .Concat(_sourceHoldFrames)
                    .OrderBy(static frame => frame.ArrivalIndex)
                    .ToArray();
            }
        }
    }

    internal void AcknowledgeAbortRestoreEnqueued(long arrivalIndex)
    {
        lock (_messageFollowGate)
        {
            lock (_gate)
            {
                if (_sourcePhase != ZLinkActorSourceHandoffPhase.AbortRestoring)
                    throw new InvalidOperationException(
                        $"Actor '{actorId}' does not have an active abort restore.");
                var sourceIndex = _frames.Count == 0
                    ? -1
                    : _frames.FindIndex(frame => frame.ArrivalIndex == arrivalIndex);
                var holdIndex = _sourceHoldFrames.Count == 0
                    ? -1
                    : _sourceHoldFrames.FindIndex(
                        frame => frame.ArrivalIndex == arrivalIndex);
                var nextArrival = _frames
                    .Concat(_sourceHoldFrames)
                    .Select(static frame => frame.ArrivalIndex)
                    .DefaultIfEmpty(long.MinValue)
                    .Min();
                if (nextArrival != arrivalIndex
                    || sourceIndex < 0 && holdIndex < 0)
                    throw new InvalidOperationException(
                        $"Actor '{actorId}' abort restore acknowledgement is out of order.");

                if (sourceIndex >= 0)
                {
                    var frame = _frames[sourceIndex];
                    _frames.RemoveAt(sourceIndex);
                    if (!_abortRestoreAdmissionsReleased)
                        _sourceIngressAdmission.Release(
                            frame.CanonicalEncodedLength);
                }
                else
                {
                    var frame = _sourceHoldFrames[holdIndex];
                    _sourceHoldFrames.RemoveAt(holdIndex);
                    if (!_abortRestoreAdmissionsReleased)
                        _sourceHoldAdmission.Release(
                            frame.CanonicalEncodedLength);
                }
            }
        }
    }

    internal void CompleteAbortCaptureRestore()
    {
        TaskCompletionSource? completion;
        lock (_messageFollowGate)
        {
            lock (_gate)
            {
                if (_sourcePhase != ZLinkActorSourceHandoffPhase.AbortRestoring)
                    throw new InvalidOperationException(
                        $"Actor '{actorId}' does not have an abort restore to complete.");
                if (_frames.Count != 0 || _sourceHoldFrames.Count != 0)
                    throw new InvalidOperationException(
                        $"Actor '{actorId}' abort restore still has queued frames.");
                _sourcePhase = ZLinkActorSourceHandoffPhase.Idle;
                _sourceIngressAdmission.ReleaseAll();
                _sourceHoldAdmission.ReleaseAll();
                _targetIngressAdmission.ReleaseAll();
                _frames.Clear();
                _sourceHoldFrames.Clear();
                _importedFrameCount = 0;
                _sourceTrailingImported = false;
                _sourceCaptureSealed = false;
                _sourceCommittedFrameCount = -1;
                _sourceCommittedHoldCount = 0;
                _abortRestoreAdmissionsReleased = false;
                _staleSourceActor = null;
                completion = _sourceCompletion;
                _sourceCompletion = null;
                ClearMessageFollowRouteLocked();
                completion?.TrySetResult();
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
        out ZLinkActorMessageFollowRoute? messageFollowRoute)
    {
        lock (_messageFollowGate)
        {
            lock (_gate)
            {
                var targetActor = default(ZLinkBackendActorRef);
                var route = ResolveFrameRouteLocked(currentActor, frameActor, out targetActor);
                messageFollowRoute = route == ZLinkActorFrameRoute.MessageFollow
                    ? _messageFollowRoute
                    : null;
                return route;
            }
        }
    }

    public void Reset()
    {
        lock (_messageFollowGate)
        {
            lock (_gate)
            {
                _sourcePhase = ZLinkActorSourceHandoffPhase.Idle;
                _targetPhase = ZLinkActorTargetHandoffPhase.Idle;
                _sourceIngressAdmission.ReleaseAll();
                _sourceHoldAdmission.ReleaseAll();
                _targetIngressAdmission.ReleaseAll();
                _frames.Clear();
                _sourceHoldFrames.Clear();
                _importedFrameCount = 0;
                _sourceTrailingImported = false;
                _sourceCaptureSealed = false;
                _sourceCommittedFrameCount = -1;
                _sourceCommittedHoldCount = 0;
                _handoffId = null;
                _joinRequest = null;
                _preparation = null;
                _canonicalMaintenanceDrain = null;
                _sourceCompletion?.TrySetResult();
                _sourceCompletion = null;
                _staleSourceActor = null;
                ClearMessageFollowRouteLocked();
            }
        }
    }

    public void AbortRuntimeGeneration(Exception failure)
    {
        ArgumentNullException.ThrowIfNull(failure);
        TaskCompletionSource<ZLinkRemoteActorJoinReply>? preparation;
        lock (_messageFollowGate)
        {
            lock (_gate)
            {
                preparation = _preparation;
                _sourcePhase = ZLinkActorSourceHandoffPhase.Idle;
                _targetPhase = ZLinkActorTargetHandoffPhase.Idle;
                _sourceIngressAdmission.ReleaseAll();
                _sourceHoldAdmission.ReleaseAll();
                _targetIngressAdmission.ReleaseAll();
                _frames.Clear();
                _sourceHoldFrames.Clear();
                _importedFrameCount = 0;
                _sourceTrailingImported = false;
                _sourceCaptureSealed = false;
                _sourceCommittedFrameCount = -1;
                _sourceCommittedHoldCount = 0;
                _handoffId = null;
                _joinRequest = null;
                _preparation = null;
                _canonicalMaintenanceDrain = null;
                _sourceCompletion?.TrySetException(failure);
                _sourceCompletion = null;
                _staleSourceActor = null;
                ClearMessageFollowRouteLocked();
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
        if (_messageFollowRoute is { } messageFollowRoute)
        {
            if (!messageFollowRoute.Lease.IsActive)
            {
                ClearMessageFollowRouteLocked();
            }
            else if (messageFollowRoute.SourceActor.NodeRid == frameActor.NodeRid
                     && messageFollowRoute.SourceActor.Generation == frameActor.Generation)
            {
                targetActor = messageFollowRoute.TargetActor;
                return ZLinkActorFrameRoute.MessageFollow;
            }
        }

        if (_staleSourceActor is { } stale
            && stale.NodeRid == frameActor.NodeRid
            && stale.Generation == frameActor.Generation)
            return _sourcePhase == ZLinkActorSourceHandoffPhase.MessageFollowCommitted
                ? ZLinkActorFrameRoute.MessageFollowExpired
                : ZLinkActorFrameRoute.Stale;

        if (targetActor.NodeRid == frameActor.NodeRid
            && targetActor.Generation == frameActor.Generation)
            return ZLinkActorFrameRoute.Current;

        return ZLinkActorFrameRoute.Stale;
    }

    private async Task RemoveMessageFollowRouteAfterDelayAsync(
        TimeSpan duration,
        CancellationTokenSource expiry)
    {
        try
        {
            await Task.Delay(duration, timeProvider, expiry.Token).ConfigureAwait(false);
            lock (_messageFollowGate)
            {
                lock (_gate)
                {
                    if (!ReferenceEquals(_messageFollowExpiry, expiry)) return;

                    ClearMessageFollowRouteLocked();
                    diagnostic?.Invoke("message_follow_route_removed entries=0");
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

    private void ClearMessageFollowRouteLocked()
    {
        _messageFollowRoute?.Lease.Cancel();
        _messageFollowRoute = null;
        var expiry = _messageFollowExpiry;
        _messageFollowExpiry = null;
        expiry?.Cancel();
    }
}

internal readonly record struct ZLinkActorMessageFollowRoute(
    ZLinkBackendActorRef SourceActor,
    ZLinkBackendActorRef TargetActor,
    string TargetMeshName,
    ulong SourceNodeGeneration,
    ulong TargetNodeGeneration,
    ulong SourceAuthorityOwnerGeneration,
    ulong TargetAuthorityOwnerGeneration,
    ulong SourceOwnerLeaseGeneration,
    ulong TargetOwnerLeaseGeneration,
    ZLinkActorMessageFollowLease Lease);

internal sealed class ZLinkActorMessageFollowLease(TimeProvider timeProvider)
{
    private readonly object _gate = new();
    private ZLinkActorMessageFollowLeasePhase _phase;
    private long _committedAt;
    private TimeSpan _duration;

    public bool IsActive
    {
        get
        {
            lock (_gate)
            {
                return _phase switch
                {
                    ZLinkActorMessageFollowLeasePhase.Pending => true,
                    ZLinkActorMessageFollowLeasePhase.Committed =>
                        timeProvider.GetElapsedTime(_committedAt) < _duration,
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
                return _phase == ZLinkActorMessageFollowLeasePhase.Committed
                       && timeProvider.GetElapsedTime(_committedAt) < _duration;
        }
    }

    public void Commit(TimeSpan duration)
    {
        lock (_gate)
        {
            if (_phase != ZLinkActorMessageFollowLeasePhase.Pending)
                throw new InvalidOperationException("Actor Message Follow lease is not pending.");
            _committedAt = timeProvider.GetTimestamp();
            _duration = duration;
            _phase = ZLinkActorMessageFollowLeasePhase.Committed;
        }
    }

    public void Cancel()
    {
        lock (_gate) _phase = ZLinkActorMessageFollowLeasePhase.Cancelled;
    }
}

internal enum ZLinkActorMessageFollowLeasePhase
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
    AbortRestoring,
    MessageFollowCommitted,
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
    AdmissionOpenDraining,
    Completed,
    Quarantined,
    RolledBack
}
