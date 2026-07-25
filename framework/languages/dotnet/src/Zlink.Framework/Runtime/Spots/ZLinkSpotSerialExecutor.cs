namespace Zlink.Framework.Runtime.Spots;

internal sealed class ZLinkSpotSerialExecutor : IAsyncDisposable
{
    private readonly ZLinkSpotActivation _activation;
    private readonly Func<bool> _isDisposed;
    private readonly Func<bool> _flowCaptureEnabled;
    private readonly ZLinkSerialExecutionQueue _queue;
    private readonly ZLinkUserSpotExecutionMode _executionMode;
    private readonly object _laneGate = new();
    private readonly Dictionary<string, ZLinkSerialExecutionQueue> _actorLanes =
        new(StringComparer.Ordinal);
    private readonly Dictionary<string, ZLinkSerialExecutionQueue> _timerLanes =
        new(StringComparer.Ordinal);
    private readonly IZLinkRuntimeFailureReporter _errorSink;
    private readonly CancellationToken _stopToken;
    private readonly object _executionOwner;
    private readonly ZLinkRuntimeTaskRunner _taskRunner;
    private readonly object _barrierGate = new();
    private ZLinkExecutionBarrierState? _relocationBarrier;
    private ulong _nextBarrierGeneration = 1;
    private int _activeApplicationClaims;
    private int _stopping;

    public ZLinkSpotSerialExecutor(
        ZLinkSpotActivation activation,
        Func<bool> isDisposed,
        CancellationToken stopToken,
        IZLinkRuntimeFailureReporter errorSink,
        Func<bool>? flowCaptureEnabled = null,
        object? executionOwner = null,
        ZLinkUserSpotExecutionMode executionMode = ZLinkUserSpotExecutionMode.SpotWide)
    {
        _activation = activation;
        _isDisposed = isDisposed;
        _flowCaptureEnabled = flowCaptureEnabled ?? AlwaysDisabled;
        _executionMode = executionMode;
        _errorSink = errorSink;
        _stopToken = stopToken;
        _executionOwner = executionOwner ?? activation?.RuntimeExecutionOwner ?? new object();
        _taskRunner = new ZLinkRuntimeTaskRunner(
            _errorSink,
            _stopToken,
            _executionOwner);
        _queue = CreateQueue(countMetrics: true);
    }

    private ZLinkSerialExecutionQueue CreateQueue(bool countMetrics)
    {
        return new ZLinkSerialExecutionQueue(
            _taskRunner,
            _errorSink,
            _stopToken,
            spotMetricKind: countMetrics ? "user" : null);
    }

    private static bool AlwaysDisabled() => false;

    public async ValueTask DisposeAsync()
    {
        Interlocked.Exchange(ref _stopping, 1);
        await _queue.DisposeAsync().ConfigureAwait(false);
        ZLinkSerialExecutionQueue[] lanes;
        lock (_laneGate)
        {
            lanes = _actorLanes.Values
                .Concat(_timerLanes.Values)
                .ToArray();
            _actorLanes.Clear();
            _timerLanes.Clear();
        }
        foreach (var lane in lanes)
            await lane.DisposeAsync().ConfigureAwait(false);
        await _taskRunner.StopAsync().ConfigureAwait(false);
    }

    public void RequestStop()
    {
        Interlocked.Exchange(ref _stopping, 1);
        _queue.Complete();
        lock (_laneGate)
        {
            foreach (var lane in _actorLanes.Values) lane.Complete();
            foreach (var lane in _timerLanes.Values) lane.Complete();
        }
    }

    public async ValueTask ExecuteAsync(
        Func<ZLinkSpotActivation, CancellationToken, ValueTask> operation,
        CancellationToken cancellationToken)
    {
        if (_isDisposed()) return;

        var claim = AcquireApplicationClaim();
        await RunClaimedAsync(
                _queue,
                ct => ExecuteOperationAsync(operation, null, ct),
                claim,
                cancellationToken)
            .ConfigureAwait(false);
    }

    public ValueTask ExecuteActorAsync<TState>(
        string actorId,
        Func<ZLinkSpotActivation, TState, CancellationToken, ValueTask> operation,
        TState state,
        CancellationToken cancellationToken)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(actorId);
        if (Volatile.Read(ref _stopping) != 0 || _isDisposed())
            return ValueTask.CompletedTask;
        var claim = AcquireApplicationClaim();
        var lane = GetLane(_actorLanes, actorId);
        return RunClaimedAsync(
            lane,
            ct => _executionMode == ZLinkUserSpotExecutionMode.SpotWide
                ? _queue.RunAsync(
                    innerCt => ExecuteActorOperationAsync(
                        actorId,
                        operation,
                        state,
                        innerCt),
                    ct)
                : ExecuteActorOperationAsync(actorId, operation, state, ct),
            claim,
            cancellationToken);
    }

    public ValueTask ExecuteTimerAsync<TState>(
        string timerName,
        Func<ZLinkSpotActivation, TState, CancellationToken, ValueTask> operation,
        TState state,
        CancellationToken cancellationToken)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(timerName);
        if (Volatile.Read(ref _stopping) != 0 || _isDisposed())
            return ValueTask.CompletedTask;
        var claim = AcquireApplicationClaim();
        if (_executionMode == ZLinkUserSpotExecutionMode.SpotWide)
            return RunClaimedAsync(
                _queue,
                ct => ExecuteTimerOperationAsync(operation, state, ct),
                claim,
                cancellationToken);

        return RunClaimedAsync(
            GetLane(_timerLanes, timerName),
            ct => ExecuteTimerOperationAsync(operation, state, ct),
            claim,
            cancellationToken);
    }

    public bool TryRunDetached(
        string name,
        Func<CancellationToken, ValueTask> operation)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(name);
        ArgumentNullException.ThrowIfNull(operation);
        return Volatile.Read(ref _stopping) == 0
               && !_isDisposed()
               && _taskRunner.TryRunDetached(name, operation);
    }

    public async ValueTask ExecuteLifecycleAsync(
        Func<ZLinkSpotActivation, CancellationToken, ValueTask> operation,
        CancellationToken cancellationToken)
    {
        if (_isDisposed()) return;

        await _queue.RunAsync(
                _ => ExecuteLifecycleOperationAsync(operation, cancellationToken),
                cancellationToken)
            .ConfigureAwait(false);
    }

    internal async ValueTask<bool> ExecuteQuiescentLifecycleAsync(
        Func<ZLinkSpotActivation, CancellationToken, ValueTask<bool>> operation,
        CancellationToken cancellationToken)
    {
        if (!TryBeginRelocationBarrier(
                holdAcceptedIngress: false,
                out var barrier))
            throw new InvalidOperationException(
                "SPOT execution lanes are already sealed.");

        try
        {
            await _queue.RunAsync(
                    _ =>
                    {
                        MarkBarrierBoundary(barrier.Generation);
                        return ValueTask.CompletedTask;
                    },
                    cancellationToken)
                .ConfigureAwait(false);
            await barrier.Quiescent.Task
                .WaitAsync(cancellationToken)
                .ConfigureAwait(false);
            var result = false;
            await ExecuteLifecycleAsync(
                    async (activation, ct) =>
                        result = await operation(activation, ct)
                            .ConfigureAwait(false),
                    cancellationToken)
                .ConfigureAwait(false);
            if (!result)
                AbortBarrier(barrier.Generation);
            return result;
        }
        catch
        {
            AbortBarrier(barrier.Generation);
            throw;
        }
    }

    private async ValueTask ExecuteLifecycleOperationAsync(
        Func<ZLinkSpotActivation, CancellationToken, ValueTask> operation,
        CancellationToken cancellationToken)
    {
        using var flow = ZLinkFlowContext.Enter(
            null,
            null,
            _flowCaptureEnabled(),
            ZLinkFlowOrigin.Lifecycle);
        await ExecuteOperationAsync(
                operation,
                null,
                cancellationToken,
                yieldAllowed: false)
            .ConfigureAwait(false);
    }

    public async ValueTask ExecuteAsync<TState>(
        Func<ZLinkSpotActivation, TState, CancellationToken, ValueTask> operation,
        TState state,
        CancellationToken cancellationToken)
    {
        if (_isDisposed()) return;

        var claim = AcquireApplicationClaim();
        await RunClaimedAsync(
                _queue,
                ct => ExecuteOperationAsync(operation, state, ct),
                claim,
                cancellationToken)
            .ConfigureAwait(false);
    }

    public bool Queue(
        Func<ZLinkSpotActivation, CancellationToken, ValueTask> operation,
        Action? onSkipped = null)
    {
        var claim = TryAcquireApplicationClaim();
        if (claim is null) return false;
        if (_queue.TryPost(
                async ct =>
                {
                    try
                    {
                        await ExecuteOperationAsync(operation, onSkipped, ct)
                            .ConfigureAwait(false);
                    }
                    finally
                    {
                        claim.Release();
                    }
                },
                out _))
            return true;

        claim.Release();
        return false;
    }

    public bool QueueAccepted(
        ReadOnlyMemory<byte> acceptedJournalRecord,
        Func<ZLinkSpotActivation, CancellationToken, ValueTask> operation,
        Action relocationRelease,
        out Task completion)
    {
        lock (_barrierGate)
        {
            if (_relocationBarrier is { HoldAcceptedIngress: false })
            {
                completion = Task.CompletedTask;
                return false;
            }
            if (_queue.TryPostAccepted(
                    acceptedJournalRecord,
                    async ct =>
                    {
                        var claim = AcquireAdmittedApplicationClaim();
                        try
                        {
                            await ExecuteOperationAsync(
                                    operation,
                                    relocationRelease,
                                    ct)
                                .ConfigureAwait(false);
                        }
                        finally
                        {
                            claim.Release();
                        }
                    },
                    relocationRelease,
                    out var item))
            {
                completion = item.Completion;
                return true;
            }
        }
        completion = Task.CompletedTask;
        return false;
    }

    internal bool TrySealRelocation(out ZLinkSpotExecutionRelocationSeal seal)
    {
        if (!TryBeginRelocationBarrier(
                holdAcceptedIngress: true,
                out var barrier))
        {
            seal = null!;
            return false;
        }
        if (!_queue.TrySealRelocation(out var queueSeal))
        {
            AbortBarrier(barrier.Generation);
            seal = null!;
            return false;
        }
        MarkBarrierBoundary(barrier.Generation);
        if (!barrier.Quiescent.Task.IsCompleted)
        {
            _queue.TryAbortRelocation(queueSeal);
            AbortBarrier(barrier.Generation);
            seal = null!;
            return false;
        }

        seal = new ZLinkSpotExecutionRelocationSeal(
            barrier.Generation,
            queueSeal);
        return true;
    }

    internal async ValueTask<ZLinkSpotExecutionRelocationSeal> SealRelocationAsync(
        CancellationToken cancellationToken)
    {
        if (!TryBeginRelocationBarrier(
                holdAcceptedIngress: true,
                out var barrier))
            throw new InvalidOperationException(
                "SPOT execution lanes are already sealed for relocation.");

        ZLinkSerialRelocationSeal? queueSeal = null;
        try
        {
            queueSeal = await _queue
                .SealRelocationAsync(cancellationToken)
                .ConfigureAwait(false);
            MarkBarrierBoundary(barrier.Generation);
            await barrier.Quiescent.Task
                .WaitAsync(cancellationToken)
                .ConfigureAwait(false);
            return new ZLinkSpotExecutionRelocationSeal(
                barrier.Generation,
                queueSeal);
        }
        catch
        {
            if (queueSeal is not null)
                _queue.TryAbortRelocation(queueSeal);
            AbortBarrier(barrier.Generation);
            throw;
        }
    }

    internal bool TryAbortRelocation(ZLinkSpotExecutionRelocationSeal seal)
    {
        ArgumentNullException.ThrowIfNull(seal);
        lock (_barrierGate)
        {
            if (_relocationBarrier?.Generation != seal.Generation)
                return false;
            if (!_queue.TryAbortRelocation(seal.QueueSeal))
                return false;
            _relocationBarrier = null;
            return true;
        }
    }

    internal bool TryCommitRelocation(
        ZLinkSpotExecutionRelocationSeal seal,
        out IReadOnlyList<ZLinkAcceptedWorkRecord> held)
    {
        ArgumentNullException.ThrowIfNull(seal);
        lock (_barrierGate)
        {
            if (_relocationBarrier?.Generation != seal.Generation)
            {
                held = [];
                return false;
            }
            if (!_queue.TryCommitRelocation(seal.QueueSeal, out held))
                return false;
            _relocationBarrier = null;
            Interlocked.Exchange(ref _stopping, 1);
            return true;
        }
    }

    private async ValueTask ExecuteOperationAsync(
        Func<ZLinkSpotActivation, CancellationToken, ValueTask> operation,
        Action? onSkipped,
        CancellationToken cancellationToken)
    {
        if (_isDisposed())
        {
            onSkipped?.Invoke();
            return;
        }

        using var _ = ZLinkSpotAmbientContext.Push(_activation);
        using var execution = PushExecutionScope(
            actorId: null,
            _executionMode == ZLinkUserSpotExecutionMode.SpotWide);
        await operation(_activation, cancellationToken).ConfigureAwait(false);
    }

    private async ValueTask ExecuteOperationAsync<TState>(
        Func<ZLinkSpotActivation, TState, CancellationToken, ValueTask> operation,
        TState state,
        CancellationToken cancellationToken)
    {
        if (_isDisposed()) return;

        using var _ = ZLinkSpotAmbientContext.Push(_activation);
        using var execution = PushExecutionScope(
            actorId: null,
            _executionMode == ZLinkUserSpotExecutionMode.SpotWide);
        await operation(_activation, state, cancellationToken).ConfigureAwait(false);
    }

    private async ValueTask ExecuteOperationAsync(
        Func<ZLinkSpotActivation, CancellationToken, ValueTask> operation,
        Action? onSkipped,
        CancellationToken cancellationToken,
        bool yieldAllowed)
    {
        if (_isDisposed())
        {
            onSkipped?.Invoke();
            return;
        }

        using var _ = ZLinkSpotAmbientContext.Push(_activation);
        using var execution = PushExecutionScope(actorId: null, yieldAllowed);
        await operation(_activation, cancellationToken).ConfigureAwait(false);
    }

    private async ValueTask ExecuteActorOperationAsync<TState>(
        string actorId,
        Func<ZLinkSpotActivation, TState, CancellationToken, ValueTask> operation,
        TState state,
        CancellationToken cancellationToken)
    {
        if (_isDisposed()) return;
        using var _ = ZLinkSpotAmbientContext.Push(_activation);
        using var execution = PushExecutionScope(
            actorId,
            _executionMode == ZLinkUserSpotExecutionMode.SpotWide);
        await operation(_activation, state, cancellationToken).ConfigureAwait(false);
    }

    private async ValueTask ExecuteTimerOperationAsync<TState>(
        Func<ZLinkSpotActivation, TState, CancellationToken, ValueTask> operation,
        TState state,
        CancellationToken cancellationToken)
    {
        if (_isDisposed()) return;
        using var _ = ZLinkSpotAmbientContext.Push(_activation);
        using var execution = PushExecutionScope(
            actorId: null,
            _executionMode == ZLinkUserSpotExecutionMode.SpotWide);
        await operation(_activation, state, cancellationToken).ConfigureAwait(false);
    }

    private IDisposable PushExecutionScope(string? actorId, bool yieldAllowed)
    {
        return ZLinkApplicationExecutionContext.Push(
            new ZLinkApplicationExecutionScope(
                _activation?.SpotId ?? "test-spot",
                _executionMode,
                actorId,
                yieldAllowed,
                _activation is null ? null : _activation.ContainsActor));
    }

    private ZLinkSerialExecutionQueue GetLane(
        Dictionary<string, ZLinkSerialExecutionQueue> lanes,
        string key)
    {
        lock (_laneGate)
        {
            if (lanes.TryGetValue(key, out var lane)) return lane;
            lane = CreateQueue(countMetrics: false);
            lanes.Add(key, lane);
            return lane;
        }
    }

    private async ValueTask RunClaimedAsync(
        ZLinkSerialExecutionQueue lane,
        Func<CancellationToken, ValueTask> operation,
        ZLinkExecutionClaim claim,
        CancellationToken cancellationToken)
    {
        ZLinkSerialWorkItem item;
        try
        {
            item = await lane.PostAsync(
                    async ct =>
                    {
                        try
                        {
                            await operation(ct).ConfigureAwait(false);
                        }
                        finally
                        {
                            claim.Release();
                        }
                    },
                    cancellationToken)
                .ConfigureAwait(false);
        }
        catch
        {
            claim.Release();
            throw;
        }

        // Caller cancellation stops only the wait. The admitted callback and a
        // yielded continuation retain the application claim until their actual
        // terminal completion, so relocation cannot capture overlapping state.
        await item.Completion.WaitAsync(cancellationToken).ConfigureAwait(false);
    }

    private ZLinkExecutionClaim AcquireApplicationClaim()
    {
        return TryAcquireApplicationClaim()
               ?? throw new ZLinkFrameworkException(
                   ZLinkFrameworkErrorKind.RequestRejected,
                   "SPOT application admission is sealed for relocation.");
    }

    private ZLinkExecutionClaim? TryAcquireApplicationClaim()
    {
        lock (_barrierGate)
        {
            if (_relocationBarrier is not null)
                return null;
            _activeApplicationClaims++;
            return new ZLinkExecutionClaim(this);
        }
    }

    private ZLinkExecutionClaim AcquireAdmittedApplicationClaim()
    {
        lock (_barrierGate)
        {
            _activeApplicationClaims++;
            if (_relocationBarrier is { } barrier)
                barrier.ActiveClaims++;
            return new ZLinkExecutionClaim(this);
        }
    }

    private void ReleaseApplicationClaim()
    {
        lock (_barrierGate)
        {
            if (_activeApplicationClaims <= 0)
                throw new InvalidOperationException(
                    "SPOT execution claim count is inconsistent.");
            _activeApplicationClaims--;
            if (_relocationBarrier is { } barrier
                && --barrier.ActiveClaims == 0
                && barrier.BoundaryReached)
                barrier.Quiescent.TrySetResult();
        }
    }

    private bool TryBeginRelocationBarrier(
        bool holdAcceptedIngress,
        out ZLinkExecutionBarrierState barrier)
    {
        lock (_barrierGate)
        {
            if (_relocationBarrier is not null
                || _nextBarrierGeneration == ulong.MaxValue)
            {
                barrier = null!;
                return false;
            }

            barrier = new ZLinkExecutionBarrierState(
                _nextBarrierGeneration++,
                _activeApplicationClaims,
                holdAcceptedIngress);
            _relocationBarrier = barrier;
            return true;
        }
    }

    private void MarkBarrierBoundary(ulong generation)
    {
        lock (_barrierGate)
        {
            if (_relocationBarrier is not { } barrier
                || barrier.Generation != generation)
                return;
            barrier.BoundaryReached = true;
            if (barrier.ActiveClaims == 0)
                barrier.Quiescent.TrySetResult();
        }
    }

    private void AbortBarrier(ulong generation)
    {
        lock (_barrierGate)
        {
            if (_relocationBarrier?.Generation == generation)
                _relocationBarrier = null;
        }
    }

    private sealed class ZLinkExecutionClaim(ZLinkSpotSerialExecutor owner)
    {
        private int _released;

        public void Release()
        {
            if (Interlocked.Exchange(ref _released, 1) == 0)
                owner.ReleaseApplicationClaim();
        }
    }

    private sealed class ZLinkExecutionBarrierState(
        ulong generation,
        int activeClaims,
        bool holdAcceptedIngress)
    {
        public ulong Generation { get; } = generation;

        public int ActiveClaims { get; set; } = activeClaims;

        public bool HoldAcceptedIngress { get; } = holdAcceptedIngress;

        public bool BoundaryReached { get; set; }

        public TaskCompletionSource Quiescent { get; } =
            new(TaskCreationOptions.RunContinuationsAsynchronously);
    }
}

internal sealed record ZLinkSpotExecutionRelocationSeal(
    ulong Generation,
    ZLinkSerialRelocationSeal QueueSeal);
