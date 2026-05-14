using System.Threading.Channels;

namespace Zlink.Framework.Runtime.Core;

internal sealed class ZLinkSerialExecutionQueue : IAsyncDisposable
{
    private const int DefaultCapacity = 4096;

    private readonly ZLinkRuntimeTaskRunner _taskRunner;
    private readonly IZLinkRuntimeErrorSink _errorSink;
    private readonly CancellationToken _executionToken;
    private readonly int _capacity;
    private readonly Channel<ZLinkSerialWorkItem> _queue =
        Channel.CreateUnbounded<ZLinkSerialWorkItem>(
            new UnboundedChannelOptions
            {
                SingleReader = true,
                SingleWriter = false,
                AllowSynchronousContinuations = false,
            });
    private readonly SemaphoreSlim _drainGate = new(1, 1);
    private readonly TaskCompletionSource _drained =
        new(TaskCreationOptions.RunContinuationsAsynchronously);
    private int _pendingCount;
    private int _completed;
    private int _drainScheduled;

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

    public async ValueTask<ZLinkSerialWorkItem> PostAsync(
        Func<CancellationToken, ValueTask> callback,
        CancellationToken cancellationToken)
    {
        if (!TryReserveSlot())
        {
            throw new InvalidOperationException("ZLink serial execution queue is full.");
        }

        var item = new ZLinkSerialWorkItem(callback);
        try
        {
            await _queue.Writer.WriteAsync(item, cancellationToken).ConfigureAwait(false);
        }
        catch
        {
            CompletePendingItem();
            throw;
        }

        ScheduleDrain();
        return item;
    }

    public bool TryPost(
        Func<CancellationToken, ValueTask> callback,
        out ZLinkSerialWorkItem item)
    {
        if (!TryReserveSlot())
        {
            item = null!;
            return false;
        }

        item = new ZLinkSerialWorkItem(callback);
        if (_queue.Writer.TryWrite(item))
        {
            ScheduleDrain();
            return true;
        }

        CompletePendingItem();
        return false;
    }

    private bool TryReserveSlot()
    {
        while (true)
        {
            var current = Volatile.Read(ref _pendingCount);
            if (current >= _capacity)
            {
                return false;
            }

            if (Interlocked.CompareExchange(ref _pendingCount, current + 1, current) == current)
            {
                return true;
            }
        }
    }

    public async ValueTask RunAsync(
        Func<CancellationToken, ValueTask> callback,
        CancellationToken cancellationToken)
    {
        var item = await PostAsync(callback, cancellationToken).ConfigureAwait(false);
        await item.Completion.WaitAsync(cancellationToken).ConfigureAwait(false);
    }

    public async ValueTask DisposeAsync()
    {
        if (Interlocked.Exchange(ref _completed, 1) != 0)
        {
            return;
        }

        _queue.Writer.TryComplete();
        if (Volatile.Read(ref _pendingCount) == 0)
        {
            _drained.TrySetResult();
        }

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

    private void ScheduleDrain()
    {
        if (Volatile.Read(ref _completed) != 0
            || Interlocked.Exchange(ref _drainScheduled, 1) != 0)
        {
            return;
        }

        _taskRunner.RunDetached(
            "serial-queue-drain",
            DrainAsync);
    }

    private async ValueTask DrainAsync(CancellationToken cancellationToken)
    {
        _ = cancellationToken;
        if (!await _drainGate.WaitAsync(0, CancellationToken.None).ConfigureAwait(false))
        {
            Volatile.Write(ref _drainScheduled, 0);
            if (_queue.Reader.TryPeek(out _))
            {
                ScheduleDrain();
            }

            return;
        }

        try
        {
            while (_queue.Reader.TryRead(out var item))
            {
                await item.InvokeAsync(
                    ReportHandlerException,
                    _executionToken).ConfigureAwait(false);
                CompletePendingItem();
            }
        }
        finally
        {
            _drainGate.Release();
        }

        Volatile.Write(ref _drainScheduled, 0);
        if (_queue.Reader.TryPeek(out _))
        {
            ScheduleDrain();
        }
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
        {
            _drained.TrySetResult();
        }
    }
}
