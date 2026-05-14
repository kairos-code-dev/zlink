using System.Threading.Channels;

namespace Zlink.Framework.Runtime.Core;

internal sealed class ZLinkSerialExecutionQueue : IAsyncDisposable
{
    private readonly ZLinkRuntimeTaskRunner _taskRunner;
    private readonly IZLinkRuntimeErrorSink _errorSink;
    private readonly CancellationToken _executionToken;
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

    public ZLinkSerialExecutionQueue(
        ZLinkRuntimeTaskRunner taskRunner,
        IZLinkRuntimeErrorSink errorSink,
        CancellationToken executionToken)
    {
        _taskRunner = taskRunner;
        _errorSink = errorSink;
        _executionToken = executionToken;
    }

    public async ValueTask<ZLinkSerialWorkItem> PostAsync(
        Func<CancellationToken, ValueTask> callback,
        CancellationToken cancellationToken)
    {
        var item = new ZLinkSerialWorkItem(callback);
        Interlocked.Increment(ref _pendingCount);
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
        item = new ZLinkSerialWorkItem(callback);
        Interlocked.Increment(ref _pendingCount);
        if (_queue.Writer.TryWrite(item))
        {
            ScheduleDrain();
            return true;
        }

        CompletePendingItem();
        return false;
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
        _taskRunner.RunDetached(
            "serial-queue-drain",
            DrainAsync);
    }

    private async ValueTask DrainAsync(CancellationToken cancellationToken)
    {
        _ = cancellationToken;
        if (!await _drainGate.WaitAsync(0, CancellationToken.None).ConfigureAwait(false))
        {
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
