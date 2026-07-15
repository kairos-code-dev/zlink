using System.Threading.Channels;

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

    private readonly Channel<ZLinkSerialWorkItem> _queue =
        Channel.CreateUnbounded<ZLinkSerialWorkItem>(
            new UnboundedChannelOptions
            {
                SingleReader = true,
                SingleWriter = false,
                AllowSynchronousContinuations = false
            });

    private readonly ZLinkRuntimeTaskRunner _taskRunner;
    private int _completed;
    private int _disposed;
    private Task? _disposeTask;
    private int _drainScheduled;
    private int _pendingCount;

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
        lock (_admissionGate)
        {
            if (Interlocked.Exchange(ref _completed, 1) != 0) return;
            _queue.Writer.TryComplete();
        }
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
            if (!_queue.Writer.TryWrite(item))
            {
                if (_spotMetricKind is not null)
                    ZLinkRuntimeMetrics.RecordSpotQueueRemoved(_spotMetricKind);
                CompletePendingItem();
                item = null!;
                return false;
            }
            ScheduleDrain();
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

            Volatile.Write(ref _completed, 1);
            var metricTimestamp = _spotMetricKind is null
                ? 0
                : ZLinkRuntimeMetrics.RecordSpotQueueEnqueued(_spotMetricKind);
            item = new ZLinkSerialWorkItem(callback, metricTimestamp);
            if (!_queue.Writer.TryWrite(item))
            {
                if (_spotMetricKind is not null)
                    ZLinkRuntimeMetrics.RecordSpotQueueRemoved(_spotMetricKind);
                CompletePendingItem();
                item = null!;
                _queue.Writer.TryComplete();
                return false;
            }

            _queue.Writer.TryComplete();
            ScheduleDrain();
            return true;
        }
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
            if (_queue.Reader.TryPeek(out _))
                ScheduleDrain();
            else
                TrySignalDrained();

            return;
        }

        try
        {
            while (_queue.Reader.TryRead(out var item))
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
                if (result == ZLinkSerialWorkItemResult.Completed)
                    CompletePendingItem();
                else
                    _ = item.Completion.ContinueWith(
                        static (task, state) => ((ZLinkSerialExecutionQueue)state!).CompletePendingItem(),
                        this,
                        CancellationToken.None,
                        TaskContinuationOptions.ExecuteSynchronously,
                        TaskScheduler.Default);
            }
        }
        finally
        {
            _drainGate.Release();
        }

        Volatile.Write(ref _drainScheduled, 0);
        if (_queue.Reader.TryPeek(out _))
            ScheduleDrain();
        else
            TrySignalDrained();
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
        if (!_queue.Writer.TryWrite(item))
        {
            if (_spotMetricKind is not null)
                ZLinkRuntimeMetrics.RecordSpotQueueRemoved(_spotMetricKind);
            CompletePendingItem();
            return false;
        }

        ScheduleDrain();
        return true;
    }

    private bool TryPostCallback(Func<CancellationToken, ValueTask> callback)
    {
        return TryPost(callback, out _);
    }
}
