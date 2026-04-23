namespace Zlink.Framework;

internal sealed class ZLinkTimer : IZLinkTimer
{
    private readonly CancellationTokenSource _stopSource;
    private readonly Task _pump;
    private int _disposed;

    public ZLinkTimer(
        TimeSpan period,
        CancellationToken spotStopToken,
        Func<CancellationToken, ValueTask> onTickAsync)
    {
        _stopSource = CancellationTokenSource.CreateLinkedTokenSource(spotStopToken);
        _pump = RunAsync(period, onTickAsync, _stopSource.Token);
    }

    public bool IsDisposed => Volatile.Read(ref _disposed) != 0;

    public async ValueTask CancelAsync(CancellationToken cancellationToken = default)
    {
        _ = cancellationToken;

        if (Interlocked.Exchange(ref _disposed, 1) != 0)
        {
            return;
        }

        _stopSource.Cancel();
        try
        {
            await _pump.ConfigureAwait(false);
        }
        catch (OperationCanceledException) when (_stopSource.IsCancellationRequested)
        {
        }
        finally
        {
            _stopSource.Dispose();
        }
    }

    public ValueTask DisposeAsync()
    {
        return CancelAsync();
    }

    private static async Task RunAsync(
        TimeSpan period,
        Func<CancellationToken, ValueTask> onTickAsync,
        CancellationToken cancellationToken)
    {
        using var timer = new PeriodicTimer(period);

        while (await timer.WaitForNextTickAsync(cancellationToken).ConfigureAwait(false))
        {
            await onTickAsync(cancellationToken).ConfigureAwait(false);
        }
    }
}
