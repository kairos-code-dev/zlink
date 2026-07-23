namespace Zlink.Framework.Runtime.Execution;

internal sealed class ZLinkSerialExecutionQueue : IAsyncDisposable
{
    private const int DefaultCapacity = 4096;
    private readonly int _capacity;
    private readonly object _admissionGate = new();
    private readonly object _disposeGate = new();

    private readonly TaskCompletionSource _drained =
        new(TaskCreationOptions.RunContinuationsAsynchronously);

    private readonly SemaphoreSlim _drainGate = new(1, 1);
    private readonly IZLinkRuntimeErrorSink _errorSink;
    private readonly CancellationToken _executionToken;
    private readonly string? _spotMetricKind;
    private readonly Queue<ZLinkSerialWorkItem> _queue = new();
    private readonly ZLinkRuntimeTaskRunner _taskRunner;
    private ZLinkSerialWorkItem? _active;
    private int _completed;
    private int _disposed;
    private Task? _disposeTask;
    private int _drainScheduled;
    private int _pendingCount;
    private int _acceptedOperations;
    private ulong _nextAcceptedSequence = 1;
    private ulong _nextRelocationSerial = 1;
    private ZLinkRelocationQueueState? _relocation;
    private TaskCompletionSource<ZLinkSerialRelocationSeal>? _sealRequest;
    private bool _relocated;

    public ZLinkSerialExecutionQueue(
        ZLinkRuntimeTaskRunner taskRunner,
        IZLinkRuntimeErrorSink errorSink,
        CancellationToken executionToken,
        int capacity = DefaultCapacity,
        string? spotMetricKind = null)
    {
        _taskRunner = taskRunner;
        _errorSink = errorSink;
        _executionToken = executionToken;
        _spotMetricKind = spotMetricKind;
        _capacity = capacity > 0
            ? capacity
            : throw new ArgumentOutOfRangeException(nameof(capacity));
    }

    public ValueTask DisposeAsync()
    {
        Task task;
        TaskCompletionSource? start = null;
        lock (_disposeGate)
        {
            if (_disposeTask is null)
            {
                Volatile.Write(ref _disposed, 1);
                start = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
                _disposeTask = DisposeCoreAsync(start.Task);
            }
            task = _disposeTask;
        }
        start?.TrySetResult();
        return new ValueTask(task);
    }

    private async Task DisposeCoreAsync(Task started)
    {
        await started.ConfigureAwait(false);
        Complete();
        try
        {
            await _drained.Task.ConfigureAwait(false);
        }
        catch (OperationCanceledException)
        {
        }
        catch (ObjectDisposedException)
        {
        }

        _drainGate.Dispose();
    }

    public void Complete()
    {
        TaskCompletionSource<ZLinkSerialRelocationSeal>? pendingSeal;
        lock (_admissionGate)
        {
            if (Interlocked.Exchange(ref _completed, 1) != 0) return;
            pendingSeal = _sealRequest;
            _sealRequest = null;
            AbortRelocationUnderLock();
            if (_queue.Count > 0)
                ScheduleDrain();
        }
        pendingSeal?.TrySetException(
            new InvalidOperationException(
                "ZLink serial execution queue closed before relocation seal completed."));
        TrySignalDrained();
    }

    public ValueTask<ZLinkSerialWorkItem> PostAsync(
        Func<CancellationToken, ValueTask> callback,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();

        if (!TryPost(callback, out var item))
            throw new InvalidOperationException("ZLink serial execution queue is closed or full.");
        return ValueTask.FromResult(item);
    }

    public bool TryPost(
        Func<CancellationToken, ValueTask> callback,
        out ZLinkSerialWorkItem item)
    {
        lock (_admissionGate)
        {
            if (Volatile.Read(ref _completed) != 0 || !TryReserveSlot())
            {
                item = null!;
                return false;
            }

            var metricTimestamp = _spotMetricKind is null
                ? 0
                : ZLinkRuntimeMetrics.RecordSpotQueueEnqueued(_spotMetricKind);
            item = new ZLinkSerialWorkItem(callback, metricTimestamp);
            _queue.Enqueue(item);
            ScheduleDrain();
            return true;
        }
    }

    public bool TryPostAccepted(
        ReadOnlyMemory<byte> payload,
        Func<CancellationToken, ValueTask> callback,
        Action relocationRelease,
        out ZLinkSerialWorkItem item)
    {
        ArgumentNullException.ThrowIfNull(callback);
        ArgumentNullException.ThrowIfNull(relocationRelease);

        lock (_admissionGate)
        {
            if (Volatile.Read(ref _completed) != 0
                || _relocated
                || !TryReserveSlot())
            {
                item = null!;
                return false;
            }
            if (_nextAcceptedSequence == ulong.MaxValue)
            {
                CompletePendingItem();
                throw new InvalidOperationException(
                    "ZLink accepted-work sequence is exhausted.");
            }

            var record = new ZLinkAcceptedWorkRecord(
                _nextAcceptedSequence++,
                payload.ToArray());
            var metricTimestamp = _spotMetricKind is null
                ? 0
                : ZLinkRuntimeMetrics.RecordSpotQueueEnqueued(_spotMetricKind);
            item = new ZLinkSerialWorkItem(
                callback,
                metricTimestamp,
                record,
                relocationRelease);
            if (_relocation is null)
            {
                _queue.Enqueue(item);
                ScheduleDrain();
            }
            else
            {
                _relocation.Held.Enqueue(item);
            }
            return true;
        }
    }

    public bool TryPostFinal(
        Func<CancellationToken, ValueTask> callback,
        out ZLinkSerialWorkItem item)
    {
        lock (_admissionGate)
        {
            if (Volatile.Read(ref _completed) != 0 || !TryReserveEssentialSlot())
            {
                item = null!;
                return false;
            }

            AbortRelocationUnderLock();
            Volatile.Write(ref _completed, 1);
            var metricTimestamp = _spotMetricKind is null
                ? 0
                : ZLinkRuntimeMetrics.RecordSpotQueueEnqueued(_spotMetricKind);
            item = new ZLinkSerialWorkItem(callback, metricTimestamp);
            _queue.Enqueue(item);
            ScheduleDrain();
            return true;
        }
    }

    public bool TrySealRelocation(out ZLinkSerialRelocationSeal seal)
    {
        lock (_admissionGate)
        {
            if (_relocated
                || _relocation is not null
                || _active is not null
                || _acceptedOperations != 0
                || _sealRequest is not null
                || Volatile.Read(ref _completed) != 0)
            {
                seal = null!;
                return false;
            }
            seal = SealUnderLock();
            return true;
        }
    }

    public ValueTask<ZLinkSerialRelocationSeal> SealRelocationAsync(
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        Task<ZLinkSerialRelocationSeal> sealTask;
        lock (_admissionGate)
        {
            if (_relocated)
                throw new InvalidOperationException(
                    "ZLink serial queue owner has already relocated.");
            if (_relocation is not null)
                throw new InvalidOperationException(
                    "ZLink serial queue owner is already sealed for relocation.");
            if (Volatile.Read(ref _completed) != 0)
                throw new InvalidOperationException(
                    "ZLink serial execution queue is closed.");

            _sealRequest ??= new TaskCompletionSource<ZLinkSerialRelocationSeal>(
                TaskCreationOptions.RunContinuationsAsynchronously);
            if (_active is null && _acceptedOperations == 0)
                CompleteSealRequestUnderLock();
            else
                ScheduleDrain();
            sealTask = _sealRequest?.Task
                       ?? Task.FromResult(_relocation is not null
                           ? new ZLinkSerialRelocationSeal(
                               _relocation.Serial,
                               _relocation.Captured
                                   .Select(static item => item.AcceptedRecord!.Snapshot())
                                   .ToArray())
                           : throw new InvalidOperationException(
                               "Relocation seal completion was lost."));
        }
        return new ValueTask<ZLinkSerialRelocationSeal>(
            sealTask.WaitAsync(cancellationToken));
    }

    public bool TryAbortRelocation(ZLinkSerialRelocationSeal seal)
    {
        ArgumentNullException.ThrowIfNull(seal);
        lock (_admissionGate)
        {
            if (!Matches(seal)) return false;
            AbortRelocationUnderLock();
            ScheduleDrain();
            return true;
        }
    }

    private void AbortRelocationUnderLock()
    {
        if (_relocation is null) return;
        while (_relocation.Captured.TryDequeue(out var item))
            _queue.Enqueue(item);
        while (_relocation.Held.TryDequeue(out var item))
            _queue.Enqueue(item);
        _relocation = null;
    }

    public bool TryCommitRelocation(
        ZLinkSerialRelocationSeal seal,
        out IReadOnlyList<ZLinkAcceptedWorkRecord> held)
    {
        ArgumentNullException.ThrowIfNull(seal);
        ZLinkSerialWorkItem[] released;
        lock (_admissionGate)
        {
            if (!Matches(seal))
            {
                held = [];
                return false;
            }

            held = _relocation!.Held
                .Select(static item => item.AcceptedRecord!.Snapshot())
                .ToArray();
            released = _relocation.Captured
                .Concat(_relocation.Held)
                .ToArray();
            _relocation.Captured.Clear();
            _relocation.Held.Clear();
            _relocation = null;
            _relocated = true;
        }

        foreach (var item in released)
        {
            if (_spotMetricKind is not null)
                ZLinkRuntimeMetrics.RecordSpotQueueRemoved(_spotMetricKind);
            item.ReleaseForRelocation(ReportHandlerException);
            CompletePendingItem();
        }
        return true;
    }

    private bool Matches(ZLinkSerialRelocationSeal seal)
    {
        return _relocation is not null
               && _relocation.Serial == seal.Serial;
    }

    private bool TryReserveSlot()
    {
        while (true)
        {
            var current = Volatile.Read(ref _pendingCount);
            if (current >= _capacity) return false;

            if (Interlocked.CompareExchange(ref _pendingCount, current + 1, current) == current) return true;
        }
    }

    private bool TryReserveEssentialSlot()
    {
        while (true)
        {
            var current = Volatile.Read(ref _pendingCount);
            if (current == int.MaxValue) return false;

            if (Interlocked.CompareExchange(ref _pendingCount, current + 1, current) == current) return true;
        }
    }

    public async ValueTask RunAsync(
        Func<CancellationToken, ValueTask> callback,
        CancellationToken cancellationToken)
    {
        var item = await PostAsync(callback, cancellationToken).ConfigureAwait(false);
        await item.Completion.WaitAsync(cancellationToken).ConfigureAwait(false);
    }

    private void ScheduleDrain()
    {
        if (Interlocked.Exchange(ref _drainScheduled, 1) != 0)
            return;

        if (!_taskRunner.TryRunDetached("serial-queue-drain", DrainAsync))
            _ = DrainAsync(CancellationToken.None);
    }

    private async ValueTask DrainAsync(CancellationToken cancellationToken)
    {
        _ = cancellationToken;
        if (!await _drainGate.WaitAsync(0, CancellationToken.None).ConfigureAwait(false))
        {
            Volatile.Write(ref _drainScheduled, 0);
            if (HasQueuedWork())
                ScheduleDrain();
            else
                TrySignalDrained();

            return;
        }

        try
        {
            while (TryTakeNext(out var item))
            {
                if (_spotMetricKind is not null)
                    ZLinkRuntimeMetrics.RecordSpotQueueStarted(
                        _spotMetricKind,
                        item.MetricEnqueuedTimestamp);
                var turn = new ZLinkSerialTurn(
                    PostResume,
                    TryPostCallback,
                    ReportHandlerException,
                    _executionToken);
                var result = await item.InvokeAsync(
                    ReportHandlerException,
                    _executionToken,
                    turn).ConfigureAwait(false);
                lock (_admissionGate)
                {
                    if (ReferenceEquals(_active, item))
                        _active = null;
                }
                if (result == ZLinkSerialWorkItemResult.Completed)
                    CompletePendingItem(item);
                else
                    _ = item.Completion.ContinueWith(
                        static (task, state) =>
                        {
                            var completion = ((ZLinkSerialExecutionQueue Queue, ZLinkSerialWorkItem Item))state!;
                            completion.Queue.CompletePendingItem(completion.Item);
                        },
                        (this, item),
                        CancellationToken.None,
                        TaskContinuationOptions.ExecuteSynchronously,
                        TaskScheduler.Default);
            }
        }
        finally
        {
            lock (_admissionGate)
                _active = null;
            _drainGate.Release();
        }

        Volatile.Write(ref _drainScheduled, 0);
        if (HasQueuedWork())
            ScheduleDrain();
        else
            TrySignalDrained();
    }

    private bool TryTakeNext(out ZLinkSerialWorkItem item)
    {
        lock (_admissionGate)
        {
            if (_sealRequest is not null)
            {
                if (_acceptedOperations == 0)
                {
                    CompleteSealRequestUnderLock();
                }
                else
                {
                    if (!TryDequeueInfrastructureUnderLock(out item!))
                        return false;
                    _active = item;
                    return true;
                }
            }
            if (!_queue.TryDequeue(out item!))
                return false;
            _active = item;
            if (item.AcceptedRecord is not null)
                _acceptedOperations++;
            return true;
        }
    }

    private bool TryDequeueInfrastructureUnderLock(out ZLinkSerialWorkItem item)
    {
        item = null!;
        if (_queue.Count == 0) return false;

        var deferred = new Queue<ZLinkSerialWorkItem>();
        while (_queue.TryDequeue(out var candidate))
        {
            if (candidate.AcceptedRecord is null)
            {
                item = candidate;
                break;
            }
            deferred.Enqueue(candidate);
        }
        var remaining = new Queue<ZLinkSerialWorkItem>(_queue);
        _queue.Clear();
        while (deferred.TryDequeue(out var candidate))
            _queue.Enqueue(candidate);
        while (remaining.TryDequeue(out var candidate))
            _queue.Enqueue(candidate);
        return item is not null;
    }

    private ZLinkSerialRelocationSeal SealUnderLock()
    {
        if (_nextRelocationSerial == ulong.MaxValue)
            throw new InvalidOperationException(
                "ZLink relocation serial is exhausted.");

        var captured = new Queue<ZLinkSerialWorkItem>();
        var infrastructure = new Queue<ZLinkSerialWorkItem>();
        while (_queue.TryDequeue(out var item))
        {
            if (item.AcceptedRecord is null)
                infrastructure.Enqueue(item);
            else
                captured.Enqueue(item);
        }
        while (infrastructure.TryDequeue(out var item))
            _queue.Enqueue(item);

        var serial = _nextRelocationSerial++;
        _relocation = new ZLinkRelocationQueueState(serial, captured);
        return new ZLinkSerialRelocationSeal(
            serial,
            captured
                .Select(static item => item.AcceptedRecord!.Snapshot())
                .ToArray());
    }

    private void CompleteSealRequestUnderLock()
    {
        if (_sealRequest is null || _acceptedOperations != 0 || _active is not null)
            return;
        var request = _sealRequest;
        _sealRequest = null;
        request.TrySetResult(SealUnderLock());
    }

    private bool HasQueuedWork()
    {
        lock (_admissionGate)
            return _queue.Count > 0;
    }

    private void ReportHandlerException(Exception exception)
    {
        try
        {
            _errorSink.ReportHandlerException(exception);
        }
        catch (Exception reportException)
        {
            _taskRunner.ReportErrorSinkFailure(
                "handler-exception-report",
                reportException);
        }
    }

    private void CompletePendingItem()
    {
        _ = Interlocked.Decrement(ref _pendingCount);
        TrySignalDrained();
    }

    private void CompletePendingItem(ZLinkSerialWorkItem item)
    {
        lock (_admissionGate)
        {
            if (item.AcceptedRecord is not null)
                _acceptedOperations--;
            CompleteSealRequestUnderLock();
        }
        CompletePendingItem();
    }

    private void TrySignalDrained()
    {
        if (Volatile.Read(ref _pendingCount) == 0
            && Volatile.Read(ref _completed) != 0
            && Volatile.Read(ref _drainScheduled) == 0)
            _drained.TrySetResult();
    }

    private bool PostResume(ZLinkSerialTurn turn, Action resume)
    {
        if (Volatile.Read(ref _completed) != 0) return false;

        Interlocked.Increment(ref _pendingCount);
        var metricTimestamp = _spotMetricKind is null
            ? 0
            : ZLinkRuntimeMetrics.RecordSpotQueueEnqueued(_spotMetricKind);
        var item = new ZLinkSerialWorkItem(async _ =>
        {
            turn.ResetSuspension();
            resume();
            var ownerTask = turn.OwnerTask;
            if (ownerTask is null || ownerTask.IsCompleted) return;

            await Task.WhenAny(ownerTask, turn.Suspended).ConfigureAwait(false);
        }, metricTimestamp);
        lock (_admissionGate)
        {
            if (Volatile.Read(ref _completed) != 0)
            {
                if (_spotMetricKind is not null)
                    ZLinkRuntimeMetrics.RecordSpotQueueRemoved(_spotMetricKind);
                CompletePendingItem();
                return false;
            }
            _queue.Enqueue(item);
        }

        ScheduleDrain();
        return true;
    }

    private bool TryPostCallback(Func<CancellationToken, ValueTask> callback)
    {
        return TryPost(callback, out _);
    }

    private sealed class ZLinkRelocationQueueState(
        ulong serial,
        Queue<ZLinkSerialWorkItem> captured)
    {
        public ulong Serial { get; } = serial;

        public Queue<ZLinkSerialWorkItem> Captured { get; } = captured;

        public Queue<ZLinkSerialWorkItem> Held { get; } = new();
    }
}

internal sealed record ZLinkSerialRelocationSeal(
    ulong Serial,
    IReadOnlyList<ZLinkAcceptedWorkRecord> Captured);
