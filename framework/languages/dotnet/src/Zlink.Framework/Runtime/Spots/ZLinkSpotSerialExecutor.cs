using System.Threading.Channels;

namespace Zlink.Framework.Runtime.Spots;

internal sealed class ZLinkSpotSerialExecutor : IAsyncDisposable
{
    private readonly ZLinkSpotActivation _activation;
    private readonly Func<bool> _isDisposed;
    private readonly CancellationToken _stopToken;
    private readonly SemaphoreSlim _gate = new(1, 1);
    private readonly Channel<Func<ValueTask>> _queue =
        Channel.CreateUnbounded<Func<ValueTask>>(new UnboundedChannelOptions
        {
            SingleReader = true,
            SingleWriter = false
        });
    private readonly Task _pump;

    public ZLinkSpotSerialExecutor(
        ZLinkSpotActivation activation,
        Func<bool> isDisposed,
        CancellationToken stopToken)
    {
        _activation = activation;
        _isDisposed = isDisposed;
        _stopToken = stopToken;
        _pump = Task.Run(RunQueuedDispatchAsync);
    }

    public async ValueTask ExecuteAsync(
        Func<ZLinkSpotActivation, CancellationToken, ValueTask> operation,
        CancellationToken cancellationToken)
    {
        if (_isDisposed())
        {
            return;
        }

        await _gate.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            if (_isDisposed())
            {
                return;
            }

            using var _ = ZLinkSpotAmbientContext.Push(_activation);
            await operation(_activation, cancellationToken).ConfigureAwait(false);
        }
        finally
        {
            _gate.Release();
        }
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

        await _gate.WaitAsync(cancellationToken).ConfigureAwait(false);
        try
        {
            if (_isDisposed())
            {
                return;
            }

            using var _ = ZLinkSpotAmbientContext.Push(_activation);
            await operation(_activation, state, cancellationToken).ConfigureAwait(false);
        }
        finally
        {
            _gate.Release();
        }
    }

    public void Queue(Func<ZLinkSpotActivation, CancellationToken, ValueTask> operation)
    {
        _queue.Writer.TryWrite(() => ExecuteAsync(operation, _stopToken));
    }

    public async ValueTask DisposeAsync()
    {
        _queue.Writer.TryComplete();

        try
        {
            await _pump.ConfigureAwait(false);
        }
        catch (OperationCanceledException)
        {
        }
        catch (ObjectDisposedException)
        {
        }

        _gate.Dispose();
    }

    private async Task RunQueuedDispatchAsync()
    {
        await foreach (var operation in _queue.Reader.ReadAllAsync().ConfigureAwait(false))
        {
            try
            {
                await operation().ConfigureAwait(false);
            }
            catch (OperationCanceledException) when (_stopToken.IsCancellationRequested)
            {
            }
            catch (ObjectDisposedException)
            {
            }
        }
    }
}
