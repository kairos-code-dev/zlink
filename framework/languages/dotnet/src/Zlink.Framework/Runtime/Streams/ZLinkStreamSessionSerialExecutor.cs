namespace Zlink.Framework.Runtime.Streams;

internal sealed class ZLinkStreamSessionSerialExecutor : IAsyncDisposable
{
    private readonly ZLinkSerialExecutionQueue _queue;
    private readonly CancellationTokenSource _stopSource = new();

    public ZLinkStreamSessionSerialExecutor()
    {
        var errorSink = new ZLinkRuntimeErrorSink();
        _queue = new ZLinkSerialExecutionQueue(
            new ZLinkRuntimeTaskRunner(errorSink, _stopSource.Token),
            errorSink,
            _stopSource.Token);
    }

    public async ValueTask DisposeAsync()
    {
        _stopSource.Cancel();
        await _queue.DisposeAsync().ConfigureAwait(false);
        _stopSource.Dispose();
    }

    public bool Enqueue(Func<ValueTask> work)
    {
        return _queue.TryPost(_ => work(), out _);
    }
}