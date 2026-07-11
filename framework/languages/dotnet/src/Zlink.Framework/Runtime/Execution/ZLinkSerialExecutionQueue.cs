using System.Threading.Channels;

namespace Zlink.Framework.Runtime.Execution;

internal sealed class ZLinkSerialExecutionQueue : IAsyncDisposable
{
    private const int DefaultCapacity = 4096;
    private readonly int _capacity;
    private readonly object _admissionGate = new();

    private readonly TaskCompletionSource _drained =
        new(TaskCreationOptions.RunContinuationsAsynchronously);

    private readonly SemaphoreSlim _drainGate = new(1, 1);
    private readonly IZLinkRuntimeErrorSink _errorSink;
    private readonly CancellationToken _executionToken;

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
    private int _drainScheduled;
    private int _pendingCount;

    public ZLinkSerialExecutionQueue(
        ZLinkRuntimeTaskRunner taskRunner,
        IZLinkRuntimeErrorSink errorSink,
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

    public async ValueTask DisposeAsync()
    {
        if (Interlocked.Exchange(ref _disposed, 1) != 0) return;
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
        if (Volatile.Read(ref _pendingCount) == 0) _drained.TrySetResult();
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
            if (!_queue.Writer.TryWrite(item))
            {
                CompletePendingItem();
                item = null!;
                return false;
            }
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
            if (_queue.Reader.TryPeek(out _)) ScheduleDrain();

            return;
        }

        try
        {
            while (_queue.Reader.TryRead(out var item))
            {
                var turn = new ZLinkSerialTurn(PostResume);
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
        if (_queue.Reader.TryPeek(out _)) ScheduleDrain();
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
        if (Interlocked.Decrement(ref _pendingCount) == 0
            && Volatile.Read(ref _completed) != 0)
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
        if (!_queue.Writer.TryWrite(item))
        {
            CompletePendingItem();
            return false;
        }

        ScheduleDrain();
        return true;
    }
}
