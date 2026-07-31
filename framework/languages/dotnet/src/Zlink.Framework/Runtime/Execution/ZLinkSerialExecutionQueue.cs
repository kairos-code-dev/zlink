namespace Zlink.Framework.Runtime.Execution;

internal sealed class ZLinkSerialExecutionQueue : IAsyncDisposable
{
    private const int DefaultCapacity = 4096;
    internal const int RelocationHoldMessageLimit = 1_024;
    internal const long RelocationHoldByteLimit = 16L * 1024 * 1024;
    private const int RelocationJournalRecordHeaderBytes =
        sizeof(ulong) + sizeof(int);
    private readonly int _capacity;
    private readonly object _admissionGate = new();
    private readonly object _disposeGate = new();

    private readonly TaskCompletionSource _drained =
        new(TaskCreationOptions.RunContinuationsAsynchronously);

    private readonly SemaphoreSlim _drainGate = new(1, 1);
    private readonly IZLinkRuntimeFailureReporter _errorSink;
    private readonly CancellationToken _executionToken;
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
    private Func<int>? _sealRequestReservation;
    private bool _relocated;

    public ZLinkSerialExecutionQueue(
        ZLinkRuntimeTaskRunner taskRunner,
        IZLinkRuntimeFailureReporter errorSink,
        CancellationToken executionToken,
        int capacity = DefaultCapacity)
    {
        _taskRunner = taskRunner;
        _errorSink = errorSink;
        _executionToken = executionToken;
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
            _sealRequestReservation = null;
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

            item = new ZLinkSerialWorkItem(callback);
            _queue.Enqueue(item);
            ScheduleDrain();
            return true;
        }
    }

    public bool TryPostNext(
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

            item = new ZLinkSerialWorkItem(callback);
            var queued = _queue.ToArray();
            _queue.Clear();
            _queue.Enqueue(item);
            foreach (var existing in queued)
                _queue.Enqueue(existing);
            ScheduleDrain();
            return true;
        }
    }

    public ZLinkAcceptedWorkAdmission TryPostAccepted(
        ReadOnlyMemory<byte> payload,
        Func<CancellationToken, ValueTask> callback,
        Action relocationRelease,
        out ZLinkSerialWorkItem item) =>
        TryPostAccepted(
            payload,
            callback,
            relocationRelease,
            previousOwnerMessageFollow: false,
            out item);

    public ZLinkAcceptedWorkAdmission TryPostAccepted(
        ReadOnlyMemory<byte> payload,
        Func<CancellationToken, ValueTask> callback,
        Action relocationRelease,
        bool previousOwnerMessageFollow,
        out ZLinkSerialWorkItem item)
    {
        ArgumentNullException.ThrowIfNull(callback);
        ArgumentNullException.ThrowIfNull(relocationRelease);

        lock (_admissionGate)
        {
            if (Volatile.Read(ref _completed) != 0)
            {
                item = null!;
                return ZLinkAcceptedWorkAdmission.Closed;
            }
            if (_relocated
                || _relocation?.IngressFrozen == true
                || (_relocation is { } relocation
                    && (relocation.Held.Count
                            >= RelocationHoldMessageLimit
                        || EncodedRelocationRecordBytes(payload.Length)
                            > RelocationHoldByteLimit
                              - relocation.HeldBytes)))
            {
                item = null!;
                return ZLinkAcceptedWorkAdmission.RelocationMoving;
            }
            if (!TryReserveSlot())
            {
                item = null!;
                return ZLinkAcceptedWorkAdmission.QueueFull;
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
            item = new ZLinkSerialWorkItem(
                callback,
                record,
                relocationRelease,
                previousOwnerMessageFollow);
            if (_relocation is null)
            {
                _queue.Enqueue(item);
                ScheduleDrain();
            }
            else
            {
                _relocation.Held.Enqueue(item);
                _relocation.HeldBytes = checked(
                    _relocation.HeldBytes
                    + EncodedRelocationRecordBytes(payload.Length));
            }
            return ZLinkAcceptedWorkAdmission.Accepted;
        }
    }

    private static long EncodedRelocationRecordBytes(int payloadLength) =>
        checked(RelocationJournalRecordHeaderBytes + (long)payloadLength);

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
            item = new ZLinkSerialWorkItem(callback);
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

    internal bool TrySealRelocation(
        Func<IReadOnlyList<ZLinkAcceptedWorkRecord>, bool> admit,
        out ZLinkSerialRelocationSeal seal)
        => TrySealRelocation(0, admit, out seal, out _);

    internal bool TrySealRelocation(
        int reservedAcceptedSequences,
        Func<IReadOnlyList<ZLinkAcceptedWorkRecord>, bool> admit,
        out ZLinkSerialRelocationSeal seal,
        out ulong firstReservedSequence)
    {
        ArgumentNullException.ThrowIfNull(admit);
        if (reservedAcceptedSequences < 0)
            throw new ArgumentOutOfRangeException(nameof(reservedAcceptedSequences));
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
                firstReservedSequence = 0;
                return false;
            }
            var captured = _queue
                .Where(static item => item.AcceptedRecord is not null)
                .Select(static item => item.AcceptedRecord!.Snapshot())
                .ToArray();
            if (!admit(captured))
            {
                seal = null!;
                firstReservedSequence = 0;
                return false;
            }
            seal = SealUnderLock(reservedAcceptedSequences);
            firstReservedSequence = seal.FirstReservedSequence;
            return true;
        }
    }

    public ValueTask<ZLinkSerialRelocationSeal> SealRelocationAsync(
        CancellationToken cancellationToken) =>
        SealRelocationAsync(
            static () => 0,
            cancellationToken);

    internal ValueTask<ZLinkSerialRelocationSeal> SealRelocationAsync(
        Func<int> reserveAcceptedSequencesAtBoundary,
        CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        ArgumentNullException.ThrowIfNull(
            reserveAcceptedSequencesAtBoundary);
        Task<ZLinkSerialRelocationSeal> sealTask;
        TaskCompletionSource<ZLinkSerialRelocationSeal> request;
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
            if (_sealRequest is not null)
                throw new InvalidOperationException(
                    "ZLink serial execution queue already has a pending relocation seal.");

            request = new TaskCompletionSource<ZLinkSerialRelocationSeal>(
                TaskCreationOptions.RunContinuationsAsynchronously);
            _sealRequest = request;
            _sealRequestReservation =
                reserveAcceptedSequencesAtBoundary;
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
                                    .ToArray(),
                                _relocation.FirstReservedSequence,
                                _relocation.ReservedAcceptedSequences)
                            : throw new InvalidOperationException(
                                "Relocation seal completion was lost."));
        }
        return AwaitSealRequestAsync(
            request,
            sealTask,
            cancellationToken);
    }

    private async ValueTask<ZLinkSerialRelocationSeal> AwaitSealRequestAsync(
        TaskCompletionSource<ZLinkSerialRelocationSeal> request,
        Task<ZLinkSerialRelocationSeal> sealTask,
        CancellationToken cancellationToken)
    {
        try
        {
            return await sealTask.WaitAsync(cancellationToken)
                .ConfigureAwait(false);
        }
        catch (OperationCanceledException)
            when (cancellationToken.IsCancellationRequested)
        {
            lock (_admissionGate)
            {
                if (ReferenceEquals(_sealRequest, request))
                {
                    _sealRequest = null;
                    _sealRequestReservation = null;
                    ScheduleDrain();
                }
                else if (request.Task.IsCompletedSuccessfully
                         && Matches(request.Task.Result))
                {
                    AbortRelocationUnderLock();
                    ScheduleDrain();
                }
            }
            throw;
        }
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

    public bool TryOpenRelocationAfterMessageFollow(
        ZLinkSerialRelocationSeal seal)
    {
        ArgumentNullException.ThrowIfNull(seal);
        lock (_admissionGate)
        {
            if (!Matches(seal))
                return false;
            var relocation = _relocation!;
            while (relocation.Captured.TryDequeue(out var item))
                _queue.Enqueue(item);
            var direct = new Queue<ZLinkSerialWorkItem>();
            while (relocation.Held.TryDequeue(out var item))
            {
                if (item.PreviousOwnerMessageFollow)
                    _queue.Enqueue(item);
                else
                    direct.Enqueue(item);
            }
            while (direct.TryDequeue(out var item))
                _queue.Enqueue(item);
            _relocation = null;
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
            item.ReleaseForRelocation(ReportHandlerException);
            CompletePendingItem();
        }
        return true;
    }

    public bool TryFreezeRelocationIngress(
        ZLinkSerialRelocationSeal seal,
        out IReadOnlyList<ZLinkAcceptedWorkRecord> held)
    {
        ArgumentNullException.ThrowIfNull(seal);
        lock (_admissionGate)
        {
            if (!Matches(seal))
            {
                held = [];
                return false;
            }
            _relocation!.IngressFrozen = true;
            held = _relocation.Held
                .Select(static item => item.AcceptedRecord!.Snapshot())
                .ToArray();
            return true;
        }
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

    private ZLinkSerialRelocationSeal SealUnderLock(
        int reservedAcceptedSequences = 0)
    {
        if (_nextRelocationSerial == ulong.MaxValue)
            throw new InvalidOperationException(
                "ZLink relocation serial is exhausted.");
        if (reservedAcceptedSequences < 0)
            throw new ArgumentOutOfRangeException(
                nameof(reservedAcceptedSequences));
        if (reservedAcceptedSequences != 0
            && (_nextAcceptedSequence == ulong.MaxValue
                || checked((ulong)reservedAcceptedSequences)
                   > ulong.MaxValue - _nextAcceptedSequence))
            throw new InvalidOperationException(
                "ZLink accepted-work sequence is exhausted.");

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
        var firstReservedSequence = _nextAcceptedSequence;
        _nextAcceptedSequence = checked(
            _nextAcceptedSequence + (ulong)reservedAcceptedSequences);
        _relocation = new ZLinkRelocationQueueState(
            serial,
            captured,
            firstReservedSequence,
            reservedAcceptedSequences);
        return new ZLinkSerialRelocationSeal(
            serial,
            captured
                .Select(static item => item.AcceptedRecord!.Snapshot())
                .ToArray(),
            firstReservedSequence,
            reservedAcceptedSequences);
    }

    private void CompleteSealRequestUnderLock()
    {
        if (_sealRequest is null || _acceptedOperations != 0 || _active is not null)
            return;
        var request = _sealRequest;
        var reserveAcceptedSequencesAtBoundary =
            _sealRequestReservation;
        _sealRequest = null;
        _sealRequestReservation = null;
        try
        {
            var reservedAcceptedSequences =
                reserveAcceptedSequencesAtBoundary?.Invoke()
                ?? throw new InvalidOperationException(
                    "Relocation seal reservation callback was lost.");
            request.TrySetResult(SealUnderLock(reservedAcceptedSequences));
        }
        catch (Exception exception)
        {
            request.TrySetException(exception);
        }
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
        var item = new ZLinkSerialWorkItem(async _ =>
        {
            turn.ResetSuspension();
            resume();
            var ownerTask = turn.OwnerTask;
            if (ownerTask is null || ownerTask.IsCompleted) return;

            await Task.WhenAny(ownerTask, turn.Suspended).ConfigureAwait(false);
        });
        lock (_admissionGate)
        {
            if (Volatile.Read(ref _completed) != 0)
            {
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
        Queue<ZLinkSerialWorkItem> captured,
        ulong firstReservedSequence,
        int reservedAcceptedSequences)
    {
        public ulong Serial { get; } = serial;

        public Queue<ZLinkSerialWorkItem> Captured { get; } = captured;

        public ulong FirstReservedSequence { get; } =
            firstReservedSequence;

        public int ReservedAcceptedSequences { get; } =
            reservedAcceptedSequences;

        public Queue<ZLinkSerialWorkItem> Held { get; } = new();

        public long HeldBytes { get; set; }

        public bool IngressFrozen { get; set; }
    }
}

internal sealed record ZLinkSerialRelocationSeal(
    ulong Serial,
    IReadOnlyList<ZLinkAcceptedWorkRecord> Captured,
    ulong FirstReservedSequence = 0,
    int ReservedAcceptedSequences = 0);

internal enum ZLinkAcceptedWorkAdmission
{
    Accepted = 0,
    Closed = 1,
    QueueFull = 2,
    RelocationMoving = 3
}
