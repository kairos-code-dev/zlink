using Zlink.Framework.Runtime.Core;

namespace Zlink.Framework.Runtime.Streams;

internal sealed class ZLinkStreamSessionSerialExecutor : IAsyncDisposable
{
    private readonly CancellationTokenSource _stopSource = new();
    private readonly ZLinkSerialExecutionQueue _queue;

    public ZLinkStreamSessionSerialExecutor()
    {
        var errorSink = new ZLinkRuntimeErrorSink();
        _queue = new ZLinkSerialExecutionQueue(
            new ZLinkRuntimeTaskRunner(errorSink, _stopSource.Token),
            errorSink,
            _stopSource.Token);
    }

    public bool Enqueue(Func<ValueTask> work)
    {
        return _queue.TryPost(_ => work(), out _);
    }

    public async ValueTask DisposeAsync()
    {
        await _queue.DisposeAsync().ConfigureAwait(false);
        _stopSource.Cancel();
        _stopSource.Dispose();
    }
}
