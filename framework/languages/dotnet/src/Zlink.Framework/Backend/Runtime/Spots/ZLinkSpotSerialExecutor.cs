using Zlink.Framework.Runtime.Core;

namespace Zlink.Framework.Runtime.Spots;

internal sealed class ZLinkSpotSerialExecutor : IAsyncDisposable
{
    private readonly ZLinkSpotActivation _activation;
    private readonly Func<bool> _isDisposed;
    private readonly ZLinkSerialExecutionQueue _queue;

    public ZLinkSpotSerialExecutor(
        ZLinkSpotActivation activation,
        Func<bool> isDisposed,
        CancellationToken stopToken)
    {
        _activation = activation;
        _isDisposed = isDisposed;
        var errorSink = new ZLinkRuntimeErrorSink();
        _queue = new ZLinkSerialExecutionQueue(
            new ZLinkRuntimeTaskRunner(errorSink, stopToken),
            errorSink,
            stopToken);
    }

    public async ValueTask ExecuteAsync(
        Func<ZLinkSpotActivation, CancellationToken, ValueTask> operation,
        CancellationToken cancellationToken)
    {
        if (_isDisposed())
        {
            return;
        }

        await _queue.RunAsync(
                ct => ExecuteOperationAsync(operation, ct),
                cancellationToken)
            .ConfigureAwait(false);
    }

    public async ValueTask ExecuteAsync<TState>(
        Func<ZLinkSpotActivation, TState, CancellationToken, ValueTask> operation,
        TState state,
        CancellationToken cancellationToken)
    {
        if (_isDisposed())
        {
            return;
        }

        await _queue.RunAsync(
                ct => ExecuteOperationAsync(operation, state, ct),
                cancellationToken)
            .ConfigureAwait(false);
    }

    public void Queue(Func<ZLinkSpotActivation, CancellationToken, ValueTask> operation)
    {
        _queue.TryPost(ct => ExecuteOperationAsync(operation, ct), out _);
    }

    public async ValueTask DisposeAsync()
    {
        await _queue.DisposeAsync().ConfigureAwait(false);
    }

    private async ValueTask ExecuteOperationAsync(
        Func<ZLinkSpotActivation, CancellationToken, ValueTask> operation,
        CancellationToken cancellationToken)
    {
        if (_isDisposed())
        {
            return;
        }

        using var _ = ZLinkSpotAmbientContext.Push(_activation);
        await operation(_activation, cancellationToken).ConfigureAwait(false);
    }

    private async ValueTask ExecuteOperationAsync<TState>(
        Func<ZLinkSpotActivation, TState, CancellationToken, ValueTask> operation,
        TState state,
        CancellationToken cancellationToken)
    {
        if (_isDisposed())
        {
            return;
        }

        using var _ = ZLinkSpotAmbientContext.Push(_activation);
        await operation(_activation, state, cancellationToken).ConfigureAwait(false);
    }
}
