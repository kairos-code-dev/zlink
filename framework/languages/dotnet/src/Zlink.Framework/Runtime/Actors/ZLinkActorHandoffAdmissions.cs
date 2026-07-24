namespace Zlink.Framework.Runtime.Actors;

internal sealed class ZLinkActorHandoffAdmissions(
    TimeProvider? timeProvider = null,
    Action<string>? diagnostic = null)
{
    private const int TerminalCapacity = 1024;
    private readonly object _gate = new();
    private readonly Dictionary<string, PendingAdmission> _pending = new(StringComparer.Ordinal);
    private readonly Dictionary<string, AdmissionExecution> _admitting = new(StringComparer.Ordinal);
    private readonly Dictionary<string, TerminalOutcome> _terminal = new(StringComparer.Ordinal);
    private readonly Queue<string> _terminalOrder = new();
    private readonly TimeProvider _timeProvider = timeProvider ?? TimeProvider.System;
    private CancellationTokenSource _generationStop = new();
    private TaskCompletionSource _drainSafe = CompletedSignal();

    public Task WaitUntilDrainSafeAsync(CancellationToken cancellationToken)
    {
        Task wait;
        lock (_gate) wait = _drainSafe.Task;
        return wait.WaitAsync(cancellationToken);
    }

    public async ValueTask<ZLinkRemoteActorAdmissionReply> AdmitAsync(
        ZLinkRemoteActorAdmissionRequest request,
        string targetSpotId,
        Func<CancellationToken, ValueTask<ZLinkRemoteActorAdmissionReply>> admit,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(admit);
        AdmissionExecution execution;
        var ownsExecution = false;
        lock (_gate)
        {
            if (_pending.TryGetValue(request.HandoffId, out var pending))
            {
                if (pending.Deadline <= _timeProvider.GetUtcNow())
                    _pending.Remove(request.HandoffId);
                else
                {
                    if (!pending.Matches(request, targetSpotId))
                        throw new InvalidOperationException(
                            $"Handoff admission '{request.HandoffId}' was retried with different request data.");
                    return pending.Reply;
                }
            }

            if (_admitting.TryGetValue(request.HandoffId, out execution!))
            {
                if (!execution.Matches(request, targetSpotId))
                    throw new InvalidOperationException(
                        $"Handoff admission '{request.HandoffId}' is already assigned to another request.");
            }
            else
            {
                execution = new AdmissionExecution(request, targetSpotId);
                _admitting.Add(request.HandoffId, execution);
                MarkDrainUnsafeLocked();
                ownsExecution = true;
            }
        }

        if (!ownsExecution)
            return await execution.Task.WaitAsync(cancellationToken).ConfigureAwait(false);

        try
        {
            var reply = await admit(cancellationToken).ConfigureAwait(false);
            Register(request, targetSpotId, reply);
            execution.Complete(reply);
            return reply;
        }
        catch (Exception exception)
        {
            execution.Fail(exception);
            throw;
        }
        finally
        {
            lock (_gate)
            {
                if (_admitting.TryGetValue(request.HandoffId, out var current)
                    && ReferenceEquals(current, execution))
                    _admitting.Remove(request.HandoffId);
                TryCompleteDrainSafeLocked();
            }
        }
    }

    public bool TryGetReply(
        ZLinkRemoteActorAdmissionRequest request,
        string targetSpotId,
        out ZLinkRemoteActorAdmissionReply reply)
    {
        lock (_gate)
        {
            if (_pending.TryGetValue(request.HandoffId, out var pending))
            {
                if (pending.Deadline <= _timeProvider.GetUtcNow())
                    _pending.Remove(request.HandoffId);
                else if (pending.Matches(request, targetSpotId))
                {
                    reply = pending.Reply;
                    return true;
                }
            }
        }

        reply = null!;
        return false;
    }

    public void Register(
        ZLinkRemoteActorAdmissionRequest request,
        string targetSpotId,
        ZLinkRemoteActorAdmissionReply reply)
    {
        if (string.IsNullOrWhiteSpace(request.HandoffId))
            throw new InvalidOperationException("Remote actor admission requires a handoff id.");

        var deadline = DateTimeOffset.FromUnixTimeMilliseconds(request.DeadlineUnixTimeMilliseconds);
        if (deadline <= _timeProvider.GetUtcNow())
            throw new TimeoutException(
                $"Actor '{request.ActorId}' handoff admission deadline has expired.");

        var pending = new PendingAdmission(request, targetSpotId, deadline, reply);
        lock (_gate)
        {
            if (_pending.TryGetValue(request.HandoffId, out var existing))
            {
                if (existing.Deadline <= _timeProvider.GetUtcNow())
                {
                    _pending.Remove(request.HandoffId);
                }
                else
                {
                    if (!existing.Matches(request, targetSpotId))
                        throw new InvalidOperationException(
                            $"Handoff admission '{request.HandoffId}' is already assigned to another actor.");
                    return;
                }
            }

            _pending.Add(request.HandoffId, pending);
            if (reply.Accepted) MarkDrainUnsafeLocked();
        }

        CancellationToken generationToken;
        lock (_gate) generationToken = _generationStop.Token;
        _ = ExpireAsync(request.HandoffId, pending, generationToken);
    }

    public void BeginCommit(ZLinkRemoteActorJoinRequest request, string targetSpotId)
    {
        lock (_gate)
        {
            if (!_pending.TryGetValue(request.HandoffId, out var pending)
                || !pending.Matches(request, targetSpotId))
                throw new InvalidOperationException(
                    $"Actor '{request.ActorId}' does not have a matching pending handoff admission '{request.HandoffId}'.");
            if (pending.Deadline <= _timeProvider.GetUtcNow())
            {
                _pending.Remove(request.HandoffId);
                throw new TimeoutException(
                    $"Actor '{request.ActorId}' handoff admission '{request.HandoffId}' has expired.");
            }

            if (!pending.Reply.Accepted)
                throw new InvalidOperationException(
                    $"Actor '{request.ActorId}' handoff admission '{request.HandoffId}' was rejected.");

            pending.Committing = true;
        }
    }

    public void Complete(string handoffId)
    {
        lock (_gate)
        {
            _pending.Remove(handoffId);
            TryCompleteDrainSafeLocked();
        }
    }

    public void Abort(string handoffId)
    {
        lock (_gate)
        {
            _pending.Remove(handoffId);
            TryCompleteDrainSafeLocked();
        }
    }

    public bool TryGetJoinOutcome(
        ZLinkRemoteActorJoinRequest request,
        string targetSpotId,
        out ZLinkRemoteActorJoinReply reply)
    {
        lock (_gate)
        {
            if (_terminal.TryGetValue(request.HandoffId, out var terminal)
                && terminal.Matches(request, targetSpotId))
            {
                reply = terminal.Reply;
                return true;
            }
        }

        reply = null!;
        return false;
    }

    public void RecordJoinOutcome(
        ZLinkRemoteActorJoinRequest request,
        string targetSpotId,
        ZLinkRemoteActorJoinReply reply,
        TimeSpan? preparedCompletionTimeout = null)
    {
        lock (_gate)
        {
            if (_terminal.TryGetValue(request.HandoffId, out var existing))
            {
                if (!existing.Matches(request, targetSpotId))
                    throw new InvalidOperationException(
                        $"Handoff outcome '{request.HandoffId}' is already assigned to another transaction.");
                return;
            }

            _terminal.Add(
                request.HandoffId,
                new TerminalOutcome(
                    request,
                    targetSpotId,
                    reply,
                    reply.Accepted && preparedCompletionTimeout is { } timeout
                        ? _timeProvider.GetUtcNow() + timeout
                        : null));
            _terminalOrder.Enqueue(request.HandoffId);
            TrimTerminalOutcomesLocked();
        }
    }

    public void RejectPreparedJoinOutcome(
        ZLinkRemoteActorJoinRequest request,
        string targetSpotId,
        ZLinkRemoteActorJoinReply rejectedReply)
    {
        if (rejectedReply.Accepted)
            throw new ArgumentException("A compensated handoff outcome must be rejected.", nameof(rejectedReply));

        lock (_gate)
        {
            if (!_terminal.TryGetValue(request.HandoffId, out var terminal))
            {
                _terminal.Add(
                    request.HandoffId,
                    new TerminalOutcome(request, targetSpotId, rejectedReply, null));
                _terminalOrder.Enqueue(request.HandoffId);
                TrimTerminalOutcomesLocked();
                return;
            }

            if (!terminal.Matches(request, targetSpotId))
                throw new InvalidOperationException(
                    $"Handoff outcome '{request.HandoffId}' is already assigned to another transaction.");
            if (terminal.Phase != ZLinkActorCommitPhase.Prepared)
                return;

            terminal.Reply = rejectedReply;
            terminal.Phase = ZLinkActorCommitPhase.Rejected;
            _pending.Remove(request.HandoffId);
            TrimTerminalOutcomesLocked();
        }
    }

    public bool TryBeginCompletion(
        ZLinkRemoteActorHandoffCompletionRequest request,
        string targetSpotId)
    {
        lock (_gate)
        {
            var terminal = ValidateCompletionLocked(request, targetSpotId);
            switch (terminal.Phase)
            {
                case ZLinkActorCommitPhase.Prepared:
                    if (terminal.Completion is null)
                        terminal.Completion = request;
                    else if (!terminal.Matches(request, targetSpotId))
                        throw new InvalidOperationException(
                            $"Actor '{request.ActorId}' handoff completion '{request.HandoffId}' was retried with different frame data.");
                    terminal.Phase = ZLinkActorCommitPhase.Completing;
                    return true;
                case ZLinkActorCommitPhase.Completed:
                    if (!terminal.Matches(request, targetSpotId))
                        throw new InvalidOperationException(
                            $"Actor '{request.ActorId}' handoff completion '{request.HandoffId}' was retried with different frame data.");
                    return false;
                case ZLinkActorCommitPhase.Completing:
                    throw new InvalidOperationException(
                        $"Actor '{request.ActorId}' handoff completion is already in progress.");
                default:
                    throw new ZLinkActorHandoffRejectedException(
                        $"Actor '{request.ActorId}' handoff completion is no longer accepted.");
            }
        }
    }

    public void CancelCompletion(
        ZLinkRemoteActorHandoffCompletionRequest request,
        string targetSpotId)
    {
        lock (_gate)
        {
            var terminal = ValidateCompletionLocked(request, targetSpotId);
            if (terminal.Phase == ZLinkActorCommitPhase.Completing)
                terminal.Phase = ZLinkActorCommitPhase.Prepared;
        }
    }

    public bool TryExpirePreparedCommit(
        ZLinkRemoteActorJoinRequest request,
        string targetSpotId,
        ZLinkRemoteActorJoinReply rejectedReply)
    {
        lock (_gate)
        {
            if (!_terminal.TryGetValue(request.HandoffId, out var terminal)
                || !terminal.Matches(request, targetSpotId)
                || terminal.Phase != ZLinkActorCommitPhase.Prepared
                || terminal.PreparedCompletionDeadline is not { } deadline
                || deadline > _timeProvider.GetUtcNow())
                return false;

            terminal.Reply = rejectedReply;
            terminal.Phase = ZLinkActorCommitPhase.Expired;
            _pending.Remove(request.HandoffId);
            TrimTerminalOutcomesLocked();
            return true;
        }
    }

    public bool IsPreparedCommitPending(
        ZLinkRemoteActorJoinRequest request,
        string targetSpotId)
    {
        lock (_gate)
        {
            return _terminal.TryGetValue(request.HandoffId, out var terminal)
                   && terminal.Matches(request, targetSpotId)
                   && terminal.Reply.Accepted
                   && terminal.Phase is ZLinkActorCommitPhase.Prepared
                       or ZLinkActorCommitPhase.Completing;
        }
    }

    public void RecordCompletion(
        ZLinkRemoteActorHandoffCompletionRequest request,
        string targetSpotId)
    {
        lock (_gate)
        {
            if (!_terminal.TryGetValue(request.HandoffId, out var terminal)
                || !string.Equals(terminal.Request.ActorId, request.ActorId, StringComparison.Ordinal))
                throw new InvalidOperationException(
                    $"Actor '{request.ActorId}' does not have a terminal handoff '{request.HandoffId}'.");
            if (!terminal.Matches(request, targetSpotId, requireRecordedCompletion: false)
                || terminal.Completion is not null && !terminal.Matches(request, targetSpotId))
                throw new InvalidOperationException(
                    $"Actor '{request.ActorId}' handoff completion '{request.HandoffId}' conflicts with its terminal result.");
            if (terminal.Phase != ZLinkActorCommitPhase.Completing)
                throw new InvalidOperationException(
                    $"Actor '{request.ActorId}' handoff completion is not active.");
            terminal.Phase = ZLinkActorCommitPhase.Completed;
            TrimTerminalOutcomesLocked();
        }
    }

    private void TrimTerminalOutcomesLocked()
    {
        var candidates = _terminalOrder.Count;
        while (_terminal.Count > TerminalCapacity && candidates-- > 0)
        {
            var handoffId = _terminalOrder.Dequeue();
            if (!_terminal.TryGetValue(handoffId, out var terminal)) continue;
            if (terminal.Phase is ZLinkActorCommitPhase.Completed or ZLinkActorCommitPhase.Expired
                || !terminal.Reply.Accepted)
                _terminal.Remove(handoffId);
            else
                _terminalOrder.Enqueue(handoffId);
        }
    }

    private TerminalOutcome ValidateCompletionLocked(
        ZLinkRemoteActorHandoffCompletionRequest request,
        string targetSpotId)
    {
        if (!_terminal.TryGetValue(request.HandoffId, out var terminal)
            || !terminal.Reply.Accepted
            || !terminal.Matches(request, targetSpotId, requireRecordedCompletion: false))
            // Terminal for the source's completion reconciliation: this
            // target no longer honors the handoff (expired or replaced), so
            // retrying the completion can never succeed.
            throw new ZLinkFrameworkException(
                ZLinkFrameworkErrorKind.RequestRejected,
                $"Actor '{request.ActorId}' does not have a matching accepted handoff '{request.HandoffId}'.");
        return terminal;
    }

    public void ResetGeneration()
    {
        CancellationTokenSource stopped;
        AdmissionExecution[] admitting;
        lock (_gate)
        {
            stopped = _generationStop;
            _generationStop = new CancellationTokenSource();
            admitting = _admitting.Values.ToArray();
            _admitting.Clear();
            _pending.Clear();
            _terminal.Clear();
            _terminalOrder.Clear();
            _drainSafe.TrySetResult();
        }

        stopped.Cancel();
        stopped.Dispose();
        var failure = new InvalidOperationException(
            "Actor handoff admission belongs to a stopped framework runtime generation.");
        foreach (var execution in admitting) execution.Fail(failure);
    }

    private async Task ExpireAsync(
        string handoffId,
        PendingAdmission pending,
        CancellationToken cancellationToken)
    {
        var expired = false;
        try
        {
            var delay = pending.Deadline - _timeProvider.GetUtcNow();
            if (delay > TimeSpan.Zero)
                await Task.Delay(delay, _timeProvider, cancellationToken).ConfigureAwait(false);
        }
        catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
        {
            return;
        }

        lock (_gate)
        {
            if (_pending.TryGetValue(handoffId, out var current)
                && ReferenceEquals(current, pending)
                && !current.Committing)
            {
                _pending.Remove(handoffId);
                TryCompleteDrainSafeLocked();
                expired = true;
            }
        }
        if (expired)
            diagnostic?.Invoke(
                $"pending_admission_expired actor={pending.ActorId} handoff_id={handoffId}");
    }

    private void MarkDrainUnsafeLocked()
    {
        if (_drainSafe.Task.IsCompleted)
            _drainSafe = new TaskCompletionSource(
                TaskCreationOptions.RunContinuationsAsynchronously);
    }

    private void TryCompleteDrainSafeLocked()
    {
        if (_admitting.Count == 0
            && !_pending.Values.Any(static pending => pending.Reply.Accepted))
            _drainSafe.TrySetResult();
    }

    private static TaskCompletionSource CompletedSignal()
    {
        var signal = new TaskCompletionSource(
            TaskCreationOptions.RunContinuationsAsynchronously);
        signal.SetResult();
        return signal;
    }

    private sealed class PendingAdmission(
        ZLinkRemoteActorAdmissionRequest request,
        string targetSpotId,
        DateTimeOffset deadline,
        ZLinkRemoteActorAdmissionReply reply)
    {
        public string ActorId { get; } = request.ActorId;

        public DateTimeOffset Deadline { get; } = deadline;

        public ZLinkRemoteActorAdmissionReply Reply { get; } = reply;

        public bool Committing { get; set; }

        public bool Matches(ZLinkRemoteActorAdmissionRequest candidate, string candidateTargetSpotId)
            => string.Equals(request.ActorId, candidate.ActorId, StringComparison.Ordinal)
               && string.Equals(request.ActorType, candidate.ActorType, StringComparison.Ordinal)
               && string.Equals(request.HandoffId, candidate.HandoffId, StringComparison.Ordinal)
               && request.DeadlineUnixTimeMilliseconds == candidate.DeadlineUnixTimeMilliseconds
               && request.SourceNodeRid.AsSpan().SequenceEqual(candidate.SourceNodeRid)
               && string.Equals(request.SourceSpotId, candidate.SourceSpotId, StringComparison.Ordinal)
               && string.Equals(request.RequestContentType, candidate.RequestContentType, StringComparison.Ordinal)
               && request.Request.AsSpan().SequenceEqual(candidate.Request)
               && targetSpotId == candidateTargetSpotId;

        public bool Matches(ZLinkRemoteActorJoinRequest candidate, string candidateTargetSpotId)
            => string.Equals(request.ActorId, candidate.ActorId, StringComparison.Ordinal)
               && string.Equals(request.ActorType, candidate.ActorType, StringComparison.Ordinal)
               && request.SourceNodeRid.AsSpan().SequenceEqual(candidate.SourceNodeRid)
               && string.Equals(request.SourceSpotId, candidate.SourceSpotId, StringComparison.Ordinal)
               && string.Equals(request.RequestContentType, candidate.RequestContentType, StringComparison.Ordinal)
               && request.Request.AsSpan().SequenceEqual(candidate.Request)
               && targetSpotId == candidateTargetSpotId;
    }

    private sealed class AdmissionExecution(
        ZLinkRemoteActorAdmissionRequest request,
        string targetSpotId)
    {
        private readonly TaskCompletionSource<ZLinkRemoteActorAdmissionReply> _result = new(
            TaskCreationOptions.RunContinuationsAsynchronously);

        public Task<ZLinkRemoteActorAdmissionReply> Task => _result.Task;

        public bool Matches(ZLinkRemoteActorAdmissionRequest candidate, string candidateTargetSpotId)
            => targetSpotId == candidateTargetSpotId
               && string.Equals(request.ActorId, candidate.ActorId, StringComparison.Ordinal)
               && string.Equals(request.ActorType, candidate.ActorType, StringComparison.Ordinal)
               && string.Equals(request.HandoffId, candidate.HandoffId, StringComparison.Ordinal)
               && request.DeadlineUnixTimeMilliseconds == candidate.DeadlineUnixTimeMilliseconds
               && request.SourceNodeRid.AsSpan().SequenceEqual(candidate.SourceNodeRid)
               && string.Equals(request.SourceSpotId, candidate.SourceSpotId, StringComparison.Ordinal)
               && string.Equals(request.RequestContentType, candidate.RequestContentType, StringComparison.Ordinal)
               && request.Request.AsSpan().SequenceEqual(candidate.Request);

        public void Complete(ZLinkRemoteActorAdmissionReply reply) => _result.TrySetResult(reply);

        public void Fail(Exception exception) => _result.TrySetException(exception);
    }

    private sealed class TerminalOutcome(
        ZLinkRemoteActorJoinRequest request,
        string targetSpotId,
        ZLinkRemoteActorJoinReply reply,
        DateTimeOffset? preparedCompletionDeadline)
    {
        public ZLinkRemoteActorJoinRequest Request { get; } = request;

        public ZLinkRemoteActorJoinReply Reply { get; set; } = reply;

        public DateTimeOffset? PreparedCompletionDeadline { get; } = preparedCompletionDeadline;

        public ZLinkActorCommitPhase Phase { get; set; } = reply.Accepted
            ? ZLinkActorCommitPhase.Prepared
            : ZLinkActorCommitPhase.Rejected;

        public ZLinkRemoteActorHandoffCompletionRequest? Completion { get; set; }

        public bool Matches(ZLinkRemoteActorJoinRequest candidate, string candidateTargetSpotId)
            => targetSpotId == candidateTargetSpotId
               && ZLinkActorHandoffRequestIdentity.Matches(Request, candidate);

        public bool Matches(
            ZLinkRemoteActorHandoffCompletionRequest candidate,
            string candidateTargetSpotId,
            bool requireRecordedCompletion = true)
            => string.Equals(Request.ActorId, candidate.ActorId, StringComparison.Ordinal)
               && string.Equals(Request.HandoffId, candidate.HandoffId, StringComparison.Ordinal)
               && string.Equals(Request.SourceSpotId, candidate.SourceSpotId, StringComparison.Ordinal)
               && Request.SourceNodeRid.AsSpan().SequenceEqual(candidate.SourceNodeRid)
               && targetSpotId == candidateTargetSpotId
               && string.Equals(candidate.TargetSpotId, candidateTargetSpotId, StringComparison.Ordinal)
               && (!requireRecordedCompletion
                   || Completion is not null
                   && Completion.OperationIdHigh == candidate.OperationIdHigh
                   && Completion.OperationIdLow == candidate.OperationIdLow
                   && string.Equals(
                       Completion.ReplyContentType,
                       candidate.ReplyContentType,
                       StringComparison.Ordinal)
                   && (Completion.Reply ?? []).AsSpan().SequenceEqual(candidate.Reply ?? []))
               && (!requireRecordedCompletion
                   || Completion is not null
                   && ZLinkActorHandoffRequestIdentity.FramesEqual(Completion.Frames, candidate.Frames));
    }
}

internal enum ZLinkActorCommitPhase
{
    Prepared,
    Completing,
    Completed,
    Rejected,
    Expired
}

internal static class ZLinkActorHandoffRequestIdentity
{
    public static bool Matches(
        ZLinkRemoteActorJoinRequest left,
        ZLinkRemoteActorJoinRequest right)
    {
        return string.Equals(left.ActorId, right.ActorId, StringComparison.Ordinal)
               && string.Equals(left.ActorType, right.ActorType, StringComparison.Ordinal)
               && string.Equals(left.HandoffId, right.HandoffId, StringComparison.Ordinal)
               && BytesEqual(left.BoundSessionNodeRid, right.BoundSessionNodeRid)
               && BytesEqual(left.BoundSessionRid, right.BoundSessionRid)
               && string.Equals(left.TransferStateContentType, right.TransferStateContentType, StringComparison.Ordinal)
               && left.TransferState.AsSpan().SequenceEqual(right.TransferState)
               && string.Equals(left.RequestContentType, right.RequestContentType, StringComparison.Ordinal)
               && left.Request.AsSpan().SequenceEqual(right.Request)
               && string.Equals(left.SourceSpotId, right.SourceSpotId, StringComparison.Ordinal)
               && left.SourceNodeRid.AsSpan().SequenceEqual(right.SourceNodeRid)
               && FramesEqual(left.HandoffFrames, right.HandoffFrames);
    }

    public static bool FramesEqual(
        IReadOnlyList<ZLinkActorHandoffFrame> left,
        IReadOnlyList<ZLinkActorHandoffFrame> right)
    {
        if (left.Count != right.Count) return false;
        for (var index = 0; index < left.Count; index++)
        {
            var a = left[index];
            var b = right[index];
            if (a.ArrivalIndex != b.ArrivalIndex
                || a.RequestId != b.RequestId
                || a.Flags != b.Flags
                || !a.ReplyActorNodeRid.AsSpan().SequenceEqual(b.ReplyActorNodeRid)
                || a.ReplyActorGeneration != b.ReplyActorGeneration
                || !a.SourceNodeRid.AsSpan().SequenceEqual(b.SourceNodeRid)
                || !a.SourceSessionRid.AsSpan().SequenceEqual(b.SourceSessionRid)
                || !a.Header.AsSpan().SequenceEqual(b.Header)
                || !a.Body.AsSpan().SequenceEqual(b.Body))
                return false;
        }

        return true;
    }

    private static bool BytesEqual(byte[]? left, byte[]? right)
        => left is null ? right is null : right is not null && left.AsSpan().SequenceEqual(right);
}
