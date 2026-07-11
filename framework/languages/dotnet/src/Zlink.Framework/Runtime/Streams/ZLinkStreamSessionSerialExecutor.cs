namespace Zlink.Framework.Runtime.Streams;

internal sealed class ZLinkStreamSessionSerialExecutor : IAsyncDisposable
{
    private readonly ZLinkSerialExecutionQueue _queue;
    private readonly CancellationTokenSource _stopSource = new();
    private readonly object _stopGate = new();
    private bool _stopSourceDisposed;

    public ZLinkStreamSessionSerialExecutor(object executionOwner)
    {
        var errorSink = new ZLinkRuntimeErrorSink();
        _queue = new ZLinkSerialExecutionQueue(
            new ZLinkRuntimeTaskRunner(errorSink, _stopSource.Token, executionOwner),
            errorSink,
            _stopSource.Token);
    }

    public async ValueTask DisposeAsync()
    {
        RequestStop();
        await _queue.DisposeAsync().ConfigureAwait(false);
        lock (_stopGate)
        {
            if (_stopSourceDisposed) return;
            _stopSource.Dispose();
            _stopSourceDisposed = true;
        }
    }

    public void RequestStop()
    {
        _queue.Complete();
    }

    public void ForceStop()
    {
        _queue.Complete();
        lock (_stopGate)
        {
            if (!_stopSourceDisposed) _stopSource.Cancel();
        }
    }

    public bool Enqueue(Func<ValueTask> work)
    {
        return _queue.TryPost(_ => work(), out _);
    }

    public bool Enqueue(Func<CancellationToken, ValueTask> work)
    {
        return _queue.TryPost(work, out _);
    }

    public bool EnqueueFinal(Func<ValueTask> work)
    {
        return _queue.TryPostFinal(_ => work(), out _);
    }
}
